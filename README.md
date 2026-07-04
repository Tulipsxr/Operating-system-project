# CSN6214 – Operating Systems Group Project
## Distributed Producer-Consumer Transfer & Parallel Analytics

*Multimedia University (MMU)** **Course:** CSN6214 Operating Systems 

### 👥 Group Members & Contributions
* **Member A: SIM HEYUE**
	* *Modules Owned:** Client child process, socket handling, reassembly buffer.
	* *Validation Role:** Chunk integrity, MD5 verification.
	* *Automation:** Project `Makefile`.
* **Member B: ANGELITA A/P MOGAN**
	* *Modules Owned:** Server chunk division, fork management, file I/O, Parallel Sort.
	* *Validation Role:** Socket stress testing, error handling.
	* *Automation:** Orchestrated validation script (`run_test.sh`)

---

## 📌 Project Overview
[cite_start]This project implements a high-performance, two-machine client-server pipeline designed to handle large-scale data transfers and concurrent analytics under Linux[cite: 7]. The workflow is split into two distinct execution phases:

1.  **Part 1: Parallel Network File Transfer (Producer-Consumer)**
	* The **Server (Producer)** reads a binary file ($\ge$ 1GB) composed of `int32_t` values, partitions it into $N$ equal chunks, and establishes sequential network streams
	* The **Client (Consumer)** forks $N$ distinct child processes to read incoming chunks simultaneously directly into a shared anonymous memory-mapped (`mmap`) reassembly buffer
	* Synchronization is handled using an anonymous POSIX semaphore (`sem_t`) and shared mutexes (`pthread_mutex_t`) to eliminate polling and race conditions
2.  **Part 2: Multi-Threaded Parallel Analytics**
	* Upon reassembly validation, the client automatically uses `fork()` and `exec()` to launch the analytics binary (`./operations`)
	**Data Parallelism:** The data array is divided into $T$ contiguous blocks where concurrent threads compute local minima and maxima before applying a mutex-protected reduction
	**Task + Data Parallelism:** A multi-threaded **Parallel Merge Sort** splits subarray sorting tasks across threads concurrently, using implicit thread synchronization boundaries via `pthread_join` to safely coordinate bottom-up merge levels

---

## 🛠️ Compilation & Execution
chmod +x run_test.sh
./run_test.sh

### Prerequisites
Ensure your Ubuntu Linux environment has the standard development utilities installed:
```bash
sudo apt update
sudo apt install build-essential -y
```
