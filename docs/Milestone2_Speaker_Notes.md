# Speaker script — SD-code recovery project

These are the words to say with `gPPM_Milestone2_Clean.pptx`. One numbered
section corresponds to one slide. The same text is embedded in the PPTX's
Notes pages; in LibreOffice Impress, use View → Notes. Slides 19–24 are
hidden Q&A backups, so the normal slideshow stops at the references on
slide 18. To display one during Q&A, select it in Impress's slide pane and
use Slide → Show Slide. Natural pauses are between paragraphs. The separate
`Milestone_1_Presenter_Brief.pdf` and
`Milestone_2_Presenter_Brief.pdf` explain the project in more depth for anyone
preparing to present.

## Slide 01 — Partitioned Matrix Recovery for SD Erasure Codes

This project is based on the gPPM paper. We started by implementing its
traditional, sequential recovery method in Milestone 1. Then, in Milestone 2,
we added the paper's partitioned and parallel matrix method for SD erasure
codes. Today I'll show what problem that solves, how the implementation is
structured, and what changed in the measurements. The three numbers on this
slide are a quick preview: fewer operations in the paper's example, about 855
mebibytes per second for one of our sequential baseline settings, and up to
2.33 times the baseline speed with PPM. At the end I'll separate the work we
have completed from what we plan to do in Milestone 3.

## Slide 02 — A disk failure plus one more sector

Imagine the storage laid out across these four disks. Each square is one
sector, and sectors with the same row number belong to the same encoded
stripe row. The orange cells in disk 2 show a complete disk failure. There's
also one extra failed sector, number 13, on disk 1. This is the kind of
combined failure an SD code is designed to handle.

Notice that rows zero, one and two each have one missing sector, but the
bottom row has two. That difference matters more than it might seem. The
first three rows can each be repaired from their own local parity equation;
the last one also needs an equation involving the whole stripe.

## Slide 03 — One stripe: n disks × r rows

Here is how to read the parameters. A stripe is the entire grid being
encoded together: n disks, with r sectors from each disk. In this example,
n and r are both four, so there are sixteen sectors. We number them across
each row from left to right. For example, sector 14 is row 3 on disk 2.

The code tolerates m whole-disk failures and s extra sector failures. Here,
both m and s equal one. The last parameter on the slide, w, is the size of a
finite-field symbol; here it is eight bits. The published coefficients for
this particular example are 1 and 2. Together these choices tell us which
parity equations to construct and what failures we can recover.

## Slide 04 — H: each column is a sector

This diagram is the parity-check matrix H. The sixteen columns match the
sixteen physical sectors on the previous slide. A colored cell means that
the sector participates in that equation; a pale cell means the coefficient
is zero. The first four equations are local: each touches only one stripe
row. The bottom equation spans the whole stripe, so we call it dense. Orange
marks the failed-sector columns within those equations.

The equation H times B equals zero is how a valid encoded stripe is defined.
B is the collection of all sector contents. The encoder calculates parity
sector values so that every equation adds to zero. Recovery uses those same
equations after some of the sector contents are lost.

## Slide 05 — Separate missing from surviving

Once we know which sectors failed, we separate the columns of H into two
matrices. F contains the failed columns, and S contains the surviving
columns. We do the same with the sector data: B-F means the missing sector
contents, and B-S means the contents we still have. This is just a
rearrangement of the original parity equation; it gives us F times B-F plus
S times B-S equals zero.

In this finite field, addition is XOR, so moving the survivor contribution
to the other side does not introduce a minus sign. We can invert F and solve
for the missing data. Physically, each coefficient tells us how to combine
entire sector regions, not just a few numbers in a small matrix.

## Slide 06 — Milestone 1: sequential recovery

Milestone 1 was our reference implementation. We built the SD parity-check
matrix from the published coding coefficients, implemented the finite-field
arithmetic, separated the failed and surviving columns, inverted the failed
matrix, and recovered the missing sectors. The normal calculation order
first combines the surviving sectors into a temporary value, called T in
this equation, and then applies F inverse to obtain the missing sectors.
Here T is an intermediate data buffer, not a thread count.

This decoder is deliberately scalar and single-threaded, and we keep it
unchanged as the comparison for Milestone 2. In the paper's small example,
its normal sequence makes 35 whole-sector multiply-XOR calls. The same code
supports the three field sizes shown on the right.

## Slide 07 — Milestone 1: sequential throughput

Now we have a measured starting point, not just an algorithm on paper. This
chart shows mean useful-data throughput for sequential encoding and recovery
with a 32 MiB codeword. To keep it readable, I am showing the three settings
with one additional sector failure, s equals one; these all use GF of two to
the eight. The test suite actually covers nine combinations of m and s.

The dark bars are recovery. With one disk failure tolerated we measured about
855 MiB per second, with two about 448, and with three about 276. Encoding is
the lighter bar and follows a similar pattern. As more disk failures are
tolerated, the baseline has more matrix work to do. These numbers give us a
local sequential reference; they are not the paper's absolute throughput
numbers because the hardware and arithmetic implementations differ.

## Slide 08 — Same answer. Different work.

The paper's first useful observation is that the order of multiplication can
change the amount of expensive work. On the left is our normal baseline: we
first apply S to the large surviving sector data and then apply F inverse.
On the right, we multiply the small coefficient matrices first and only then
touch the sector data.

Both sides calculate the same answer, because matrix multiplication is
associative. But during the small matrix multiplication, some finite-field
terms can cancel and turn coefficients into zero. A zero means we skip a
whole-sector operation. That's why the paper counts nonzero coefficients as
region operations. In Milestone 2 we use matrix-first for the independent
parts of the problem; the dependent remainder still uses the normal order.

## Slide 09 — Partition by stripe row

The second observation comes from the structure of the failure pattern.
With m equal to one, each of the first three rows has exactly one missing
sector and one local equation that can recover it. So we make three separate
jobs, for sectors 2, 6 and 10. The number three here means three independent
groups; it does not necessarily mean three OpenMP workers.

The bottom row has two missing sectors, 13 and 14, but only one local
equation. It also needs the dense equation, and we call this coupled piece
the remainder. In ordinary terms, we can solve the easy rows independently,
then use those recovered sectors to help solve the row that needs more
information.

## Slide 10 — Parallel first. Dependent last.

This is the actual order of the PPM recovery function. First we validate the
inputs, then count failures by stripe row and divide the matrix into
independent jobs and a remainder. We prepare each job, including its matrix
split and inverse, before workers begin writing to any sector.

The blue outlined jobs run in an OpenMP loop, each with the matrix-first
calculation. The orange line represents the barrier at the end of that loop.
Only once every independent job finishes can the dependent remainder use
their recovered sectors; we decode that remainder with the normal sequence.
Finally, we collect the operation counts. The baseline function remains
separate; this path is implemented by ppm_recover_sd.

## Slide 11 — Parallel without races

There are four practical decisions behind that flow. First, independent jobs
write different missing sectors and read surviving data, so they do not need
locks around the sector writes. Second, the remainder has to wait for the
barrier, because it may read sectors those jobs just recovered.

Third, the work counter is thread-local. A shared counter would race, and
making every operation update an atomic counter would add contention to the
hot part of the benchmark. Fourth, we finish splitting and inverting the
matrices before any writes happen. These choices let us parallelize the
independent work while preserving the data dependencies of the original
decoding problem.

## Slide 12 — Figure 3: 35 → 29 operations

This slide shows the paper's Figure 3 example in our implementation. The
sequential baseline has 22 region operations from the surviving matrix and
13 from the inverse, for 35 altogether. With PPM, the three independent jobs
take three operations each, and the dependent remainder takes 20. That adds
up to 29, which is six fewer calls, or about 17 percent less whole-sector
work.

These bars are operation counts, not elapsed time. They show that we changed
the work required by the algorithm itself, before adding more threads. On
the next performance slide, the one-thread PPM result will let us see that
algorithmic improvement separately from the benefit of parallel workers.

## Slide 13 — Published coefficients. Synthetic bytes.

Here is where the experiment's inputs come from. The published FAST table
from Plank gives us the coding coefficients and field width for 3,969 SD
configurations. It is a table of code parameters, not a collection of real
user files. We generate the sector contents ourselves with a fixed
pseudo-random sequence, and place the disk and extra sector failures in a
repeatable pattern.

Across the parameter grid there are 7,833 feasible configuration-and-layout
cases. For performance, we use a larger 32 MiB codeword and nine SD
settings. The point of this design is that we can hold the data and failure
pattern steady while comparing the sequential and PPM recovery paths.

## Slide 14 — Fewer region operations in every tested layout

The six-operation difference in the paper's example is one instance of a
larger pattern. Across the tested layouts, the median reduction in
whole-sector operations was 12.9 percent. Grouped by m, the median reduction
increases from 4.7 percent with one disk failure tolerated, to 13.3 percent
with two, and 22.1 percent with three.

This is an algorithmic result: changing the thread count does not change the
number of sector operations. More disk-failure parity equations tend to give
the partitioned method more useful local work and more opportunities to
avoid operations. The chart describes the representative failure placements
we tested for each published configuration.

## Slide 15 — Scaling: one thread to eight

Here we look at elapsed recovery time. The plot shows the three s-equals-one
settings, with each line comparing PPM to the sequential baseline measured
in the same benchmark run. We use a 32 MiB codeword and ten trials per
setting; the speedup is the baseline median time divided by the PPM median
time.

Even with one thread, PPM is faster, which is the algorithmic benefit we
just discussed. Adding workers can bring another gain. The strongest line is
m equals three: it rises from about 1.19 times at one thread to 2.33 times at
eight. The increase isn't perfectly linear because we still have a
sequential remainder and because parallel execution has costs of its own.

## Slide 16 — The remainder limits scaling

This schematic explains why eight threads do not mean eight times the
performance. The independent groups can run at the same time, but everyone
must reach the barrier before the dependent remainder starts. That remainder
is sequential in the current implementation. The widths here are just a
diagram, not measured proportions.

There can also be thread startup and synchronization overhead, and workers
may compete for memory bandwidth while they read large sectors. So the main
claim is not ideal linear scaling. It is that the implemented SD decoder does
less region work, can use multiple cores for the independent groups, and has
a measured speedup against the sequential reference.

## Slide 17 — From PPM to generalized gPPM

This slide separates what we have built from what we would build next. So
far, Milestone 2 partitions SD-code failures, always chooses matrix-first
for the independent jobs, and runs those jobs with OpenMP. That gave us the
operation reduction and the speedup shown earlier.

Generalized gPPM would decide the calculation order dynamically from a more
detailed cost model. In that model, u counts nonzero coefficients, v counts
the ones that require real field multiplication rather than just XOR, and c
represents that extra cost. We would also compare a symmetric code such as
Reed–Solomon and accelerate the sector operations with SIMD. Those are
planned extensions, not claims about the current implementation.

## Slide 18 — Sources

Our main algorithmic reference is Li and colleagues' gPPM paper in ACM
Transactions on Architecture and Code Optimization. The SD-code design
comes from Plank, Blaum and Hafner, and Plank's published SD encoder and
decoder supplied the coefficient table we use to construct the codes. The
repository on this slide contains the implementation and the measured
results. That is the work we are presenting: a sequential Milestone 1
reference, an SD-specific partitioned and parallel Milestone 2 decoder, and
a clear path toward the remaining gPPM features.

## Slide 19 — Codebase & implementation details

This is the start of the backup slides. I would not show these during the
normal presentation. If a question goes into the repository or asks for a
specific implementation detail, I can open the relevant slide from here.
The next pages map the modules, name the programs and data files, and show
small excerpts of the Milestone 1 and Milestone 2 recovery paths.
These slides are hidden from the normal slideshow; I can select the one I
need in the slide pane and show it when a question calls for more detail.

## Slide 20 — What is in this codebase?

The input on the left is Plank's published coefficient table. The loader
reads a code configuration, and sd_code.c uses its coefficients to construct
the parity-check matrix H. There are then two ways to use that matrix: the
Milestone 1 ec_recover function in codec.c, or the Milestone 2 PPM entry point
in ppm.c. Both paths use the same GF arithmetic and matrix utilities shown
underneath. The executables at the bottom are wrappers around these shared
layers for demos, sweeps, and benchmarks. So this isn't two independent
decoders built from scratch: PPM adds partitioning and scheduling on top of
the existing mathematical foundation.

## Slide 21 — Core files: what each module owns

If someone wants a tour of src, I would start at the bottom of the stack.
gf.c provides finite-field arithmetic and the multiply-XOR operation over
whole data regions. matrix.c provides small coefficient-matrix operations,
including inversion and the matrix multiplication added for matrix-first.
coefficients.c loads the published code settings; sd_code.c builds H and
chooses the parity and failure locations. codec.c contains the split,
sequential normal decoder, matrix-first decoder, and original baseline
entry point. Finally, ppm.c owns the SD-specific partition, prepared jobs,
OpenMP loop, barrier, dependent remainder, and statistics. The matching
headers declare the interfaces.

## Slide 22 — Drivers, tests, data and results

The source modules on the previous slide are the reusable library. These
files run it. main.c is the small paper example; sweep.c and benchmark.c are
the sequential Milestone 1 experiments. ppm_sweep.c and ppm_benchmark.c do
the corresponding Milestone 2 work and timing measurements. The tests folder
has focused checks for fields, SD construction, coefficients and PPM. The
FAST text file is the published parameter input; CSV files in results are
outputs generated by the sweeps and benchmarks. The plotting script turns
those CSVs into the figures. The Makefile gives the commands at the bottom.

## Slide 23 — From the M1 decoder to PPM jobs

The excerpt on the left is the core of ec_recover in Milestone 1. It splits
the full matrix into F and S, inverts F, and calls the normal decoder for
the whole coupled problem. The right side shows what Milestone 2 adds before
and during decoding. It identifies rows whose number of failed sectors is
exactly m, counts those as independent jobs, and calls the matrix-first
decoder for each. The excerpts are reflowed to fit on the slide, but those
are the calls and conditions in the actual source. The algebra beneath them
is the key change in work order: first S times the sector blocks, versus
first F inverse times the small S coefficient matrix.

## Slide 24 — Inside the OpenMP recovery

This is the independent-job loop from ppm.c. OpenMP assigns iterations to
workers with static scheduling. Inside each iteration we reset that worker's
counter, decode the job using matrix-first, and save the count with the job.
The counter is declared thread-local in gf.h, so workers don't contend on
one shared integer. Once the loop ends, the implicit barrier means all
independent sectors have been written. Only then do we call the normal
decoder for the remainder. The small call on the right is shortened to fit;
the actual call also passes the remainder's faulty and surviving sector
indices. That order is why the dense equations never read half-recovered
data.
