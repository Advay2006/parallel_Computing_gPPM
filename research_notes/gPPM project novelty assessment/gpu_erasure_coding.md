# Prior Work on GPU-Accelerated Erasure Coding and CPU-vs-GPU Erasure Coding Comparisons

Scope note: about 20 search/fetch calls. Full text was read for: ASAP 2016 (Chen et al.), the SC17 Gibraltar/ISA-L poster (Haddock et al.), EC-Bench (Shi et al.), the ECCheck ICDCS'25 paper (not relevant, see Gaps), and the GPU-RSCode project page. For G-CRS, PErasure, Gibraltar, Khasymski et al., Kalcher & Lindenstruth, ECS2 and gPPM, only abstracts, bibliographic records or search snippets were seen. Their technique details come from those abstracts and should be checked against the full papers before they are quoted.

## Q1. Which GPU RS / CRS / XOR erasure-coding systems exist (authors, venue, year, technique, speedups)?

### Takeaway
GPU erasure coding for plain Reed-Solomon and Cauchy RS is a mature topic: Curry et al. 2008–2011 (Gibraltar), PErasure 2015, ASAP'16 OpenCL, G-CRS 2018 (TPDS), ECS2 2020 (CLUSTER). It covers table-lookup GF(2^8) kernels, bitmatrix/XOR CRS kernels in constant and shared memory, texture-cached tables, and multi-stream PCIe overlap. Nearly all of it is MDS RS/CRS over GF(2^8), or w ≤ 8 bitmatrices.

### Cited Findings
**Curry, Skjellum, Ward, Brightwell (Gibraltar line, 2008–2011)**
- "Accelerating Reed-Solomon coding in RAID systems with GPUs," IEEE IPDPS 2008, pp. 1–6 (Curry, Skjellum, Ward, Brightwell). — [SC17 poster bibliography](https://sc17.supercomputing.org/SC17%20Archive/tech_poster/poster_files/post207s2-file2.pdf); [ResearchGate](https://www.researchgate.net/publication/224316839_Accelerating_reed-solomon_coding_in_RAID_systems_with_GPUs)
- "Arbitrary dimension Reed-Solomon coding and decoding for extended RAID on GPUs," PDSW 2008, by the same four authors. It is a general RS encode/decode library for RAID-like systems. — [hgpu.org](https://hgpu.org/?p=4788)
- "Gibraltar: A Reed-Solomon coding library for storage applications on programmable graphics processors," *Concurrency and Computation: Practice and Experience* 23(18):2477–2495, 2011, DOI 10.1002/cpe.1810. It argues that CPUs of that era were poorly matched to RS and that GPUs give high performance "across a wide range of operating conditions." — [catalog record](https://ersearch2.cvtisr.sk/vufind/SummonRecord/FETCH-LOGICAL-c3690-2545e59f4a4edb18134288e8bcc28a6625afd3075233531c87a5efd359aa8a433)
- Gibraltar works over GF(2^8) only (fixed w = 8). — [EC-Bench, Shi, Lu, Panda](https://par.nsf.gov//servlets/purl/10112763)
- Gibraltar is described as table-based, and as "the most successful GPU-based RS coding library." — [G-CRS paper PDF](https://www.comp.hkbu.edu.hk/~chxw/papers/G-CRS.pdf) (seen via search snippet)
- Sandia work reports that the GPU beat a contemporary CPU by about an order of magnitude and could support at least three parity disks with no performance penalty. — [Khasymski search context / OSTI](https://www.osti.gov/biblio/1020432) (snippet)
- Curry's dissertation, "A Highly Reliable GPU-Based RAID System" (UAB), reports that Gibraltar RAID beat Linux md on RAID-6. — [dissertation](https://uab.edu/cas/cybersecurity/images/Documents/Matthew_Curry_Dissertation1.pdf)
- Open-source code exists as libgibraltar, aimed at NVIDIA GPUs of compute capability 1.3 or later, with a Jerasure fallback. — [libgibraltar mirror](https://github-users.yhajoa.workers.dev/jaredjennings/libgibraltar)
- Gibraltar was also integrated as a Ceph erasure-coding plugin and outperformed a CPU Ceph EC plugin. — [Sandia pub page](https://sandia.gov/ccr/publications/details/gpu-erasure-coding-for-campaign-storage-2017-01-01) (snippet)

**Khasymski, Rafique, Butt, Vazhkudai, Nikolopoulos (2012)**
- "On the Use of GPUs in Realizing Cost-Effective Distributed RAID," IEEE MASCOTS 2012, pp. 469–478, DOI 10.1109/MASCOTS.2012.59. It runs RAID-6 parity on client- and server-side GPUs for the Lustre parallel file system. Numbers were not retrieved. — [ORNL](https://www.ornl.gov/publication/use-gpus-realizing-cost-effective-distributed-raid); [author page](https://dsniko.github.io/publication/2012-10-01-gpu-distributed-raid)

**Kalcher & Lindenstruth (2011)**
- "Accelerating Galois Field Arithmetic for Reed-Solomon Erasure Codes in Storage Applications," IEEE CLUSTER 2011, pp. 290–298, DOI 10.1109/CLUSTER.2011.40. It does table-free, SIMD-vectorized polynomial multiplication in **GF(2^16)** on CPUs and GPGPUs. — [IEEE CSDL](https://www.computer.org/csdl/proceedings-article/cluster/2011/4516a290/12OmNxwENq9)

**PErasure: Chu, Liu, Ouyang, Yung, Liu, Leung (2015)**
- "PErasure: A Parallel Cauchy Reed-Solomon Coding Library for GPUs," IEEE ICC 2015 (IEEE Xplore doc 7248360). It is bitmatrix/XOR CRS and handles any k, m; one GPU thread produces w coding words from k data words. It also addresses the PCIe bottleneck. — [paper PDF](https://www.comp.hkbu.edu.hk/~chxw/papers/icc_2015.pdf); [IEEE](https://ieeexplore.ieee.org/document/7248360/)
- Speedups: about 10x over multithreaded Jerasure on a quad-core CPU, and 2–4x over Gibraltar, on a GTX 780. Up to about 10 GB/s overall encoding for systems tolerating up to 8 failures. — [HKBU scholars](https://scholars.hkbu.edu.hk/en/publications/perasure-a-parallel-cauchy-reed-solomon-coding-library-for-gpus-8/)

**G-CRS: Chengjian Liu, Qiang Wang, Xiaowen Chu, Yiu-Wing Leung (2018)**
- "G-CRS: GPU Accelerated Cauchy Reed-Solomon Coding," IEEE TPDS 29(7):1484–1498, 2018, DOI 10.1109/TPDS.2018.2791438. Techniques: a compact bitmatrix stored in **constant memory**, data access through **shared memory**, "decoding parallelism," and an analytical performance model. Evaluated on Maxwell and Pascal GPUs. — [paper PDF](https://www.comp.hkbu.edu.hk/~chxw/papers/G-CRS.pdf); [ACM DL](https://dl.acm.org/doi/10.1109/TPDS.2018.2791438)
- Speedups: about 10x over "most other coding libraries" and up to 3x over PErasure on the same GPU. Code is on a supplementary page. — [HKBU scholars](https://scholars.hkbu.edu.hk/en/publications/g-crs-gpu-accelerated-cauchy-reed-solomon-coding-2/); [supplement](https://www.comp.hkbu.edu.hk/~chxw/gcrs.html)

**Chen, Zhou, Shen (NCSU) and Gahm, Venkat, Booth, Marshall (Cisco), 2016**
- "OpenCL-Based Erasure Coding on Heterogeneous Architectures," IEEE ASAP 2016. It covers RS over GF(2^8) on a 28-core Xeon, a K40m GPU, an AMD Carrizo APU and a Stratix V FPGA. — [paper PDF](https://hzhou.wordpress.ncsu.edu/files/2022/12/asap16.pdf)
- It compares three GF(2^8) multiplication methods on the GPU: Russian-peasant (loop shift-and-reduce), small log/exp tables (two 256 B tables: three lookups, an add, a subtract, a modulo) and a full 64 KB table. It is **one work-item per output byte**, then vectorized to 4 and 16 bytes per thread (thread coarsening). Tables go in the **texture cache**. — [same](https://hzhou.wordpress.ncsu.edu/files/2022/12/asap16.pdf)
- Findings: the full table plus texture cache was fastest. **Russian-peasant was the slowest**, because the kernel is compute-bound, not bandwidth-bound. Profiling put most stalls on execution dependency. — [same](https://hzhou.wordpress.ncsu.edu/files/2022/12/asap16.pdf)
- It used **8 streams** to overlap PCIe transfers with compute, raising end-to-end throughput from 2.15 to 3.90 GB/s. — [same](https://hzhou.wordpress.ncsu.edu/files/2022/12/asap16.pdf)

**GPU-RSCode, Shuai Yuan, LSALab NTHU (project, no paper found)**
- GPU RS encode/decode with log/exp tables (512 B, extended to drop the modulo and the zero branch). Tables are placed in constant memory or shared memory. Uses pinned memory and up to 4 CUDA streams to overlap copies. Decoding inverts with Gaussian elimination, and the page suggests CPU-GPU cooperation for that step. Reports 14.71x over single-thread Jerasure for 1600 MB with k=4, n=6. — [project page](https://yszheda.github.io/GPU-RSCode)

**ECS2: Chan-Jung Chang, Jerry Chou, Yu-Ching Chou, I-Hsin Chung (2020)**
- "ECS2: A Fast Erasure Coding Library for GPU-Accelerated Storage Systems with Parallel & Direct IO," IEEE CLUSTER 2020, pp. 349–358, DOI 10.1109/CLUSTER49012.2020.00045. It uses GPUDirect to bypass host memory. IO latency fell 10–20% and storage IO throughput rose up to 70% on synthetic trace-based workloads. GitHub: NTHU-LSALAB/ECS2 (repo not verified). — [IEEE CSDL](https://www.computer.org/csdl/proceedings-article/cluster/2020/667700a349/1oqKEKFMFNK); [IEEE Xplore](https://ieeexplore.ieee.org/document/9229486/)

**Haddock, Curry, Bangalore, Skjellum (2017–2019)**
- "GPU Erasure Coding for Campaign Storage," HPC-IODC @ ISC'17, plus an SC17 poster. Gibraltar on K40/K80/P100 against ISA-L and Jerasure for wide stripes (k up to 120–128, m = 24). — [SC17 poster](https://sc17.supercomputing.org/SC17%20Archive/tech_poster/poster_files/post207s2-file2.pdf)
- A 2019 SpringSim paper by the same group concluded that GPGPU erasure coding on file-transfer nodes pays off when using a large number of shards. — [OSTI](https://www.osti.gov/servlets/purl/1573314) (snippet only)

### Inferences
- The project's per-thread design (one CUDA thread per output symbol, all coefficient contributions accumulated in a register, one kernel per matrix-vector product) is essentially the baseline mapping used by ASAP'16 ("workitem computing one byte of the output matrix") and by PErasure (one thread per coding-word group). It is not new.
- The "direct shift-and-reduce vs log/antilog table" comparison was done for GF(2^8) on a GPU in ASAP'16 and on the GPU-RSCode page. Both found loop/shift multiplication slower than tables, so that comparison is not new for GF(2^8).
- CUDA streams for overlapping PCIe transfers with compute are standard (ASAP'16, GPU-RSCode, PErasure's PCIe discussion).

### Gaps
- Exact DOI for ASAP 2016 (believed to be IEEE ASAP 2016, pp. 33–40). Page numbers and DOI are unverified.
- NVIDIA does not appear to ship a first-party GPU erasure-coding library. The searches found none, though NVIDIA's DOCA offers EC on BlueField DPUs, not GPUs (see Q4). Treat this as "not found," not as confirmed absence.
- No tensor-core-based erasure-coding paper was found for 2018–2026, despite a targeted search.
- G-CRS kernel details (decoding parallelism, stream use) were not read in full text.

## Q2. GPU encoding/decoding for LRC, SD, PMDS, STAIR or other asymmetric codes? PPM/gPPM on a GPU?

### Takeaway
No prior GPU implementation of SD, PMDS, STAIR or LRC encoding/decoding was found. No GPU implementation of PPM or gPPM was found either. Both PPM papers are CPU multithreading works. This is the strongest candidate for the novel part of the project, with the caveat that "not found" is not proof.

### Cited Findings
- PPM: "A partitioned and parallel matrix algorithm to accelerate encoding/decoding process of asymmetric parity erasure codes" (HIT). It partitions the parity-check matrix for fine-grained parallel recovery on CPU threads, with gains already visible at 2 threads, and applies only to asymmetric-parity codes. — [HIT scholar page](https://scholar.hit.edu.cn/en/publications/ppm-a-partitioned-and-parallel-matrix-algorithm-to-accelerate-enc/)
- gPPM (Li et al., ACM TACO 2023, DOI 10.1145/3625005) generalizes PPM to symmetric codes, chooses the calculation sequence dynamically, and reports up to 210.81% improvement over PPM. Only the abstract was seen; no GPU is mentioned in it (full text blocked, 403). — [ACM DL](https://dl.acm.org/doi/10.1145/3625005); [ResearchGate](https://www.researchgate.net/publication/374093580_gPPM_A_Generalized_Matrix_Operation_and_Parallel_Algorithm_to_Accelerate_the_EncodingDecoding_Process_of_Erasure_Codes)
- A targeted search for GPU decoding of SD/PMDS/STAIR/LRC returned only coding-theory constructions (PMDS/SD constructions, STAIR codes, LRC decoding) with no GPU implementations. — [STAIR codes arXiv](https://arxiv.org/pdf/1406.5282); [PMDS/SD constructions arXiv](https://arxiv.org/pdf/1401.4715); [2024 EC survey, TOS](https://keyuncheng.github.io/files/publications/tos24ecsurvey.pdf)
- The SC17 Gibraltar poster cites SD codes (Plank, Blaum, Hafner, FAST 2013) in its bibliography, but its GPU work is plain RS. — [SC17 poster](https://sc17.supercomputing.org/SC17%20Archive/tech_poster/poster_files/post207s2-file2.pdf)
- Every GPU library surveyed is RS or CRS MDS coding: Gibraltar (GF(2^8) RS), PErasure and G-CRS (CRS bitmatrix), ASAP'16 (RS GF(2^8)). — [EC-Bench](https://par.nsf.gov//servlets/purl/10112763); [G-CRS](https://www.comp.hkbu.edu.hk/~chxw/papers/G-CRS.pdf); [ASAP'16](https://hzhou.wordpress.ncsu.edu/files/2022/12/asap16.pdf)

### Inferences
- A GPU decoder for SD codes, and a port of gPPM-style partitioning (independent row groups plus a dependent remainder, normal vs matrix-first ordering) to a GPU, appear to be unreported in the literature found. This is the most defensible novelty claim.
- The novelty is mostly in the combination: an SD code, gPPM scheduling and a GPU together. The kernel itself is standard.

### Gaps
- No search was run in Chinese-language venues (e.g., 计算机学报), where HIT-group follow-ups to PPM/gPPM might appear.
- Patents: one USPTO patent describes the CPU precomputing an inverse matrix that the GPU then applies to the surviving units. It is close in spirit to host-side matrix preparation, but its applicability was not analyzed. — [USPTO 10230398](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/10230398)
- Google Scholar "cited by" lists for gPPM and PPM were not checked for later GPU follow-ups. This is recommended before making any claim.

## Q3. CUDA streams for independent sub-matrices or failure groups in erasure decoding?

### Takeaway
Streams in GPU erasure coding have been used for (a) PCIe copy/compute overlap via data partitioning and (b) running many small independent coding tasks concurrently (Gibraltar, SC12). No work was found that assigns streams to independent gPPM/PPM matrix partitions within a single decode.

### Cited Findings
- Zhou, Skjellum, Curry, "Evaluating Asynchrony in Gibraltar RAID's GPU Reed-Solomon Coding Library," SC Companion 2012, pp. 1496–1498. It uses an asynchronous CUDA design so multiple coding tasks run concurrently, with large speedups for small-block tasks from concurrent kernels. — [SC12 schedule](https://sc12.supercomputing.org/schedule/event_detail.php-evid=post273.html); [DBLP](https://dblp.org/rec/conf/sc/ZhouSC12a)
- ASAP'16 split data into partitions, each in its own stream (8 streams), to hide PCIe latency: 2.15 to 3.90 GB/s. — [ASAP'16](https://hzhou.wordpress.ncsu.edu/files/2022/12/asap16.pdf)
- GPU-RSCode used 1 to 4 streams plus pinned memory, which raised H2D bandwidth from about 1.4 to 5.9 GB/s. — [GPU-RSCode](https://yszheda.github.io/GPU-RSCode)
- A targeted search for PPM partitioning with CUDA streams found nothing combining the two. — [gPPM ACM](https://dl.acm.org/doi/10.1145/3625005)

### Inferences
- Concurrent kernels for independent erasure tasks are prior art (SC12). Mapping gPPM's independent row groups to streams is a natural application of that idea. It is a modest but specific contribution, not a new technique.

### Gaps
- The SC12 poster's full text was not read, so it is unconfirmed whether its tasks were independent stripes or sub-problems of one decode.

## Q4. Which papers compare GPU against multicore SIMD CPU (ISA-L / GF-Complete), and what crossovers do they report?

### Takeaway
Many headline GPU speedups use weak CPU baselines: single-thread ISA-L, single-core, or non-SIMD Jerasure. Against a properly multithreaded SIMD ISA-L, the GPU advantage shrinks to about 1.4x (ASAP'16, K40m vs a 28-core Xeon, transfers included). An independent benchmark (EC-Bench) found ISA-L beating Gibraltar for most configurations. GPUs lose at small chunks because of transfer overhead and win with wide stripes or many erasures. This agrees with the project's finding that the GPU wins rarely and narrowly.

### Cited Findings
- **ASAP'16:** multithreaded ISA-L (SSE, 56 threads on a 28-core Xeon) reached 2.83–2.84 GB/s; single-thread ISA-L reached 0.12–0.13 GB/s. The K40m reached 3.90 GB/s **including PCIe transfers with 8 streams** (2.15 GB/s without overlap). 30x33 matrix, 836.9 MB file. The GPU-to-CPU ratio is about 1.38x end to end. Without stream overlap, the GPU (2.15) was **slower** than the multithreaded CPU (2.83). — [ASAP'16](https://hzhou.wordpress.ncsu.edu/files/2022/12/asap16.pdf)
- **EC-Bench (Shi, Lu, Panda, OSU):** Jerasure 2.0, ISA-L 2.18 and Gibraltar on K80, plus Mellanox-EC. Onload coders beat offload coders for small chunks. Jerasure and ISA-L beat Gibraltar below 32 KB, and Gibraltar beat Jerasure (but not ISA-L) only above 32 KB, because large chunks amortize host-to-device transfer. Overall, "highly optimized onload coders (e.g., Intel ISA-L) outperform offload coders for most configurations." Gibraltar could not run with 32 MB or 64 MB chunks. — [EC-Bench PDF](https://par.nsf.gov//servlets/purl/10112763) (venue believed to be Bench'18, Springer LNCS 2019, unverified)
- **SC17 poster (Haddock et al.):** Gibraltar on K40 produced about 1.5 GB/s at k=128, m=24, and over 1.5 GB/s decoding with 20 erasures. ISA-L fell to 267 MB/s at 20 erasures. However, the CPU numbers are explicitly "all a single core of the CPU." — [SC17 poster](https://sc17.supercomputing.org/SC17%20Archive/tech_poster/poster_files/post207s2-file2.pdf)
- **PErasure:** about 10x over *multithreaded Jerasure* on a quad-core. That was Jerasure, not ISA-L or GF-Complete SIMD. — [HKBU](https://scholars.hkbu.edu.hk/en/publications/perasure-a-parallel-cauchy-reed-solomon-coding-library-for-gpus-8/)
- **GPU-RSCode:** 14.71x over single-thread Jerasure. — [GPU-RSCode](https://yszheda.github.io/GPU-RSCode)
- **ECS2** identifies storage-to-GPU data transfer as "the new performance bottleneck" even after the GPU speeds up coding. — [IEEE CSDL](https://www.computer.org/csdl/proceedings-article/cluster/2020/667700a349/1oqKEKFMFNK)
- Non-GPU offload can also lose to the CPU. A Sogang NVMSA 2024 poster found DOCA erasure coding on a BlueField DPU about 10.5x slower than the host. — [NVMSA 2024 poster](https://discos.sogang.ac.kr/file/2024/poster/NVMSA_2024_J_Ryu.pdf) (snippet only)

### Inferences
- The project's result (the GPU wins only GF(2^32) at 32 MiB by 1.25–1.39x including transfers, and loses at 128 MiB and for GF(2^8)/GF(2^16) against a multithreaded SIMD CPU) is consistent with the best-controlled prior comparison (ASAP'16, about 1.38x) and with EC-Bench. It is a confirmation, not a contradiction. A careful, fair-baseline comparison on 2024 hardware (Ada laptop GPU vs Meteor Lake) is still a modest empirical contribution, because most published GPU speedups use single-thread or non-SIMD baselines.
- The CPU winning at 128 MiB is not explained by the literature found. It may relate to laptop-GPU memory or PCIe limits, or to CPU cache/bandwidth effects.

### Gaps
- No paper was found that systematically sweeps field width (w = 8/16/32) on the GPU against the CPU, or that reports a "GPU only helps for wide fields" crossover. The project's field-width finding appears new as an empirical observation. A plausible reason (inference only): CPU SIMD tricks such as ISA-L/GF-Complete PSHUFB split tables are highly optimized for w = 8 and lose efficiency at w = 32, while the GPU cost per symbol grows more slowly.
- No GPU-vs-GF-Complete (multi-w SIMD) comparison was found.

## Q5. GF(2^16) / GF(2^32) arithmetic on the GPU

### Takeaway
GPU work at wide fields is sparse. Kalcher & Lindenstruth (CLUSTER 2011) did table-free vectorized GF(2^16) on GPGPUs. Gibraltar, ISA-L and ASAP'16 are fixed at GF(2^8), and the CRS libraries use w-bit bitmatrices. No GPU paper found evaluates GF(2^32) split tables or log tables for erasure coding.

### Cited Findings
- Kalcher & Lindenstruth 2011: SIMDized polynomial (table-free) multiplication in GF(2^16) for RS erasure codes on CPUs and GPGPU accelerators. — [IEEE CSDL](https://www.computer.org/csdl/proceedings-article/cluster/2011/4516a290/12OmNxwENq9)
- Gibraltar and ISA-L both fix w = 8. — [EC-Bench](https://par.nsf.gov//servlets/purl/10112763)
- CPU background for wide fields: GF-Complete (Plank et al., UT-CS-13-717) covers SPLIT-TABLE for GF(2^32) (e.g., 4,32 and 8,8 splits), with 8,8 split the fastest single multiply on CPU. — [GF-Complete tech report](https://web.eecs.utk.edu/~jplank/plank/papers/UT-CS-13-717.pdf); [Plank, Greenan, Miller, "Screaming Fast Galois Field Arithmetic Using Intel SIMD Instructions," FAST 2013](https://web.eecs.utk.edu/~jplank/plank/papers/FAST-2013-GF.html)
- Composite-field GF(2^16)/GF(2^32) on processors (Greenan, Miller, Schwarz, MASCOTS 2008). — [UCSC PDF](https://users.soe.ucsc.edu/~elm/Papers/mascots08.pdf)

### Inferences
- The project's GPU variants for GF(2^16) (log/antilog) and GF(2^32) (split-byte tables) appear to be among the few reported GPU erasure-coding evaluations at w = 16 and 32. A GF(2^32) GPU evaluation in particular was not found anywhere.

### Gaps
- Full text of Kalcher & Lindenstruth was not read, so their GPU throughput numbers are unknown.
- GPU carry-less-multiply or GFNI-like approaches for wide fields were not searched in depth (e.g., cryptography-oriented GPU GF(2^n) work).
- The HKU ECCheck paper (Qi, Li, Wu, Peng, Zheng, ICDCS 2025) uses CRS erasure coding for in-memory DNN checkpoints. From the parts read, it is a systems paper, not a GPU-kernel paper. Whether it encodes on the GPU was not confirmed. — [ICDCS'25 PDF](https://i.cs.hku.hk/~cwu/papers/gcqi-icdcs25.pdf)
