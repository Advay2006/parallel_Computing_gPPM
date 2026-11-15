# Final presentation — speaker notes

The same notes are embedded in the PowerPoint Notes pages. Times and results refer to the final paper and retained benchmark data.

## Slide 01 — Faster recovery, on CPUs and GPUs

Today we will continue from the first two milestones, which have already been presented.
The focus is what we added afterwards: cost-selected CPU recovery, GPU offloading,
and the final profiling and scaling study. The goal is to understand not just whether
parallelism helps, but which approach is appropriate for a particular recovery workload.
We will briefly recap the baseline, show the new design, and finish with measured
results and bottlenecks.

## Slide 02 — A quick recap

In Milestone 1, we implemented the sequential Sector–Disk recovery baseline.
The decoder builds the parity-check equations, separates failed sectors from survivors,
and applies matrix operations over a finite field to reconstruct the missing data.
In Milestone 2, we used the code structure to partition row-local failures into
independent groups. OpenMP workers can recover those groups concurrently, after which
the dependent remainder is solved. That is the hand-off to the new work: we keep
the same recovery problem, but choose the computation more carefully and optimize
how the arithmetic runs on CPUs and GPUs.

## Slide 03 — Choose the cheaper route

The first Milestone 3 change is about doing less work. The normal sequence applies
the survivor matrix to data, then applies the inverse failure matrix. Matrix-first
instead combines the small coefficient matrices before applying the result to the
large data regions. Both produce the same recovered sectors, but their operation
counts can differ. Our selector estimates the work from the number of nonzero
coefficients and the additional cost of multiplications by values other than one.
It also compares the partitioned approach with whole-matrix recovery. This is a
heuristic for arithmetic work, not a prediction of total runtime or thread overhead.

## Slide 04 — Three layers of CPU optimization

The CPU implementation adds three complementary layers. First, cost selection
reduces estimated arithmetic work. Second, SIMD kernels operate on multiple field
symbols at once. Third, we parallelize the dependent remainder by dividing data
into disjoint byte ranges; each worker applies the same transformation to its range.
In the initial layer study, this specific GF two-to-the-eight case reaches 1.53 times
the scalar baseline after cost selection, 15.06 times with SIMD, and 25.42 times
with the parallel remainder at eight threads. These are cumulative ratios to the
single-threaded scalar normal baseline, not three independent speedups. Across all
nine configurations the final range is 1.69 to 34.85 times.

## Slide 05 — More threads are not always faster

The final experiment compares one, two, four and eight threads within the same
measurement run. This example uses a 32 MiB stripe, two failed disks and three
additional sector failures, in the 32-bit field. The two-thread median is 32.12
milliseconds; the eight-thread median is 37.49 milliseconds. So adding workers
does not guarantee an improvement. The best of the four settings gives between
1.37 and 2.90 times the one-thread L3 speed across the nine 32 MiB workloads.
These are medians from ten trials, and the best setting is selected from those same
trials. Scheduling, the heterogeneous CPU, calibration and variation can affect the ranking.

## Slide 06 — GPU recovery flow

On the GPU path, the host still prepares the matrices and selects the recovery
sequence. Each recovery allocates device memory, copies the stripe and required
tables, runs the matrix-vector kernels, then copies the stripe back and frees
resources. Independent groups are submitted to separate CUDA streams. They finish
before the dependent remainder starts. Each CUDA thread handles one output symbol.
We compare direct shift-and-reduce multiplication with a table-based variant. The
direct variant is still massively parallel on the GPU; it is not a single-threaded
GPU implementation. Importantly, our reported GPU time includes preparation,
allocation, transfers, synchronization and cleanup, rather than kernel time alone.

## Slide 07 — The final evaluation

For the final evaluation, we compared four CPU thread settings with both GPU
variants using the same encoded data and failure pattern within a configuration.
The platform is an Intel Core Ultra 9 185H and an RTX 4050 Laptop GPU. We ran all
nine combinations of one to three failed disks and one to three additional sector
failures at 32 MiB. At eight and 128 MiB we tested the three cases with two failed
disks. Each variant warmed once and then ran ten measured trials; order was shuffled
within each round. All 900 trials passed recovered-byte, checksum and parity
syndrome validation. This is one machine and one session, and the three stripe-size
suites were run separately in a fixed order.

## Slide 08 — CPU versus GPU at 32 MiB

Here we compare end-to-end medians from the final 32 MiB experiment for the
two-disk-failure cases. Blue is the fastest median among the four tested CPU
thread counts; teal is the GPU table variant. The CPU is much faster for the
8-bit and 16-bit fields. In the 32-bit field, the best tested CPU takes 32.12
milliseconds and the table GPU takes 24.48 milliseconds, a 1.31 times gain.
The other two 32-bit cases yield 1.25 and 1.39 times. This is not a claim that
the GPU always wins, or that we exhaustively tuned the CPU. The field width and
the number of extra sector failures also change together in these workloads.

## Slide 09 — Stripe size changes the comparison

The 32-bit-field example gives a useful counterpoint. At eight MiB, the GPU table
variant takes 8.47 milliseconds compared with 21.88 for the best tested CPU.
At 32 MiB, the GPU still wins: 24.48 against 32.12 milliseconds. At 128 MiB,
the CPU takes 59.47 milliseconds and the GPU takes 83.26 milliseconds, so the
CPU wins that measured case by about 1.40 times. We should not claim an exact
hardware crossover point. These size suites ran in separate processes in the
fixed order 32, 8, then 128 MiB, with fresh calibration. Run order, scheduling,
system state, and selecting the best CPU thread count from the same trials can
all affect the observed ranking.

## Slide 10 — Profiling and bottlenecks

We profiled recovery separately from the native timing experiment. On the CPU,
Callgrind counted instructions for a one-thread GF two-to-the-thirty-two recovery.
The scalar field-multiply routine accounts for 58.2 percent of instructions; 57.2
percentage points are attributed to calls from the SIMD region kernel, where
coefficient tables are rebuilt. Reusing these tables is a concrete candidate for
future work. These instruction shares are not native time shares. For the GPU
table variant in the same workload, Nsight records a kernel timeline union of
11.80 milliseconds and summed copy intervals of 10.12 milliseconds, with 17
kernels and 52 copies. These categories and host synchronization can overlap, so
we cannot add them to obtain total latency. The traces point to computation,
transfer and repeated setup as targets; they do not prove a single root cause.

## Slide 11 — Conclusions

The main takeaway is to choose the recovery path for the workload rather than
assuming that more parallelism or GPU offload always wins. Cost selection and
partitioning reduce or reorganize the work; SIMD and threads accelerate the CPU
implementation; and GPU offload helps some, but not all, of the tested cases.
The final profiling points to repeated CPU coefficient-table construction and
GPU kernel, transfer, and setup activity as practical optimization targets.
Next we would test coefficient-table reuse and persistent GPU buffers or batching,
then repeat the study across more stripe sizes, failure layouts and sessions.
All reported measurements and the limitations are documented in the final paper.
Thank you; we are happy to take questions.
