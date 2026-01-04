// mk_iocp_tiled_sieve_strided_fastdiv.c
//
// Strided tiling + carried offsets + FastDiv (mulhi) to reduce idiv usage.
// - Workers block on GetQueuedCompletionStatus()
// - Minimality preserved: best_m shrinks end_limit; epoch completes when all workers exhaust <= end_limit
// - Strided tile assignment: no global atomic allocator hotspot
// - Carried offsets: removes per-tile base%p
// - FastDiv: removes idiv from the inner "divide out p factors" loop for odd primes
//
// Build (clang-cl, x64 dev prompt):
//   clang-cl /nologo /O2 /W4 /GS- mk_iocp_tiled_sieve_strided_fastdiv.c /link kernel32.lib /OPT:REF /OPT:ICF /lld
//
// Build (clang, in VS dev prompt):
//   clang -O3 -std=c11 -march=native mk_iocp_tiled_sieve_strided_fastdiv.c -o mk.exe -lkernel32 -fuse-ld=lld
//
// Usage:
//   mk.exe [K=200] [threads=0=HW] [tile_len=65536] [batch_tiles=128]

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__clang__) || defined(__GNUC__)
#define FORCEINLINE __attribute__((always_inline)) inline
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define FORCEINLINE __forceinline
#define LIKELY(x)   (x)
#define UNLIKELY(x) (x)
#endif

#ifndef FASTDIV_2X_CORRECT
// Set to 1 for extra safety (still far cheaper than idiv). You can set to 0 for max speed after validation.
#define FASTDIV_2X_CORRECT 1
#endif

// ------------------------------------------------------------
// FastDivU32 (u64 / u32, u64 % u32) using mulhi + 1-2 corrections
// ------------------------------------------------------------

typedef struct FastDivU32 {
	uint32_t d;
	uint64_t mul; // floor(2^64 / d) for odd d, and 2^63 for d=2
} FastDivU32;

static FORCEINLINE uint64_t mulhi_u64(uint64_t a, uint64_t b) {
#if defined(__SIZEOF_INT128__)
	return (uint64_t)(((__uint128_t)a * (__uint128_t)b) >> 64);
#else
#error "__uint128_t is required (use clang/clang-cl x64)."
#endif
}

static FORCEINLINE FastDivU32 fastdiv_u32_make_prime(uint32_t d) {
	FastDivU32 fd;
	fd.d = d;
	if (d == 2) fd.mul = 1ull << 63;
	else        fd.mul = UINT64_MAX / (uint64_t)d; // valid for odd d
	return fd;
}

static FORCEINLINE void fastdiv_u32_divmod(const FastDivU32* fd, uint64_t n, uint64_t* q, uint32_t* r) {
	uint64_t d = (uint64_t)fd->d;

	uint64_t q0 = mulhi_u64(n, fd->mul);
	uint64_t rr = n - q0 * d;

	if (rr >= d) { rr -= d; ++q0; }
#if FASTDIV_2X_CORRECT
	if (rr >= d) { rr -= d; ++q0; }
#endif

	* q = q0;
	*r = (uint32_t)rr;
}

static FORCEINLINE uint32_t fastdiv_u32_mod(const FastDivU32* fd, uint64_t n) {
	uint64_t q; uint32_t r;
	fastdiv_u32_divmod(fd, n, &q, &r);
	return r;
}

static FORCEINLINE int fastdiv_u32_divide_if_divisible(const FastDivU32* fd, uint64_t* n_io) {
	uint64_t q; uint32_t r;
	fastdiv_u32_divmod(fd, *n_io, &q, &r);
	if (LIKELY(r != 0)) return 0;
	*n_io = q;
	return 1;
}

// ------------------------------------------------------------
// Prime list up to k
// ------------------------------------------------------------

typedef struct PrimeList {
	uint32_t* p;
	uint32_t  count;
} PrimeList;

static PrimeList primes_upto(uint32_t n) {
	PrimeList out = { 0 };
	if (n < 2) return out;

	uint8_t* mark = (uint8_t*)calloc((size_t)n + 1, 1);
	if (!mark) return out;

	for (uint32_t i = 2; (uint64_t)i * (uint64_t)i <= n; ++i) {
		if (mark[i]) continue;
		for (uint32_t j = i * i; j <= n; j += i) mark[j] = 1;
	}

	uint32_t cnt = 0;
	for (uint32_t i = 2; i <= n; ++i) if (!mark[i]) ++cnt;

	uint32_t* p = (uint32_t*)malloc((size_t)cnt * sizeof(uint32_t));
	if (!p) { free(mark); return out; }

	uint32_t w = 0;
	for (uint32_t i = 2; i <= n; ++i) if (!mark[i]) p[w++] = i;

	free(mark);
	out.p = p;
	out.count = cnt;
	return out;
}

static void primes_free(PrimeList* pl) {
	free(pl->p);
	pl->p = NULL;
	pl->count = 0;
}

// ------------------------------------------------------------
// Bitset helpers
// ------------------------------------------------------------

static FORCEINLINE void bitset_clear(uint8_t* bits, uint32_t bit_count) {
	memset(bits, 0, (bit_count + 7u) >> 3);
}
static FORCEINLINE void bitset_set(uint8_t* bits, uint32_t i) {
	bits[i >> 3] |= (uint8_t)(1u << (i & 7));
}
static FORCEINLINE uint32_t bitset_get(const uint8_t* bits, uint32_t i) {
	return (uint32_t)((bits[i >> 3] >> (i & 7)) & 1u);
}

// ------------------------------------------------------------
// IOCP epoch system
// ------------------------------------------------------------

enum { KEY_START = 1, KEY_STOP = 2 };

typedef struct Epoch {
	uint32_t  k;
	uint32_t  tile_len;
	uint64_t  step;         // tile_len * thread_count

	uint64_t  start_m;      // inclusive
	uint64_t  end_m;        // inclusive

	PrimeList primes;

	// Precomputed per-k arrays (same length as primes.count)
	FastDivU32* fd;         // [prime_count]
	uint32_t* step_mod;   // [prime_count]  (step % p)

	volatile LONG64 best_m;     // global min found
	volatile LONG64 end_limit;  // shrinks to best_m-1

	volatile LONG active_workers;
	HANDLE evt_done;
} Epoch;

typedef struct JobSystem {
	HANDLE iocp;
	uint32_t thread_count;
	Epoch epoch;
} JobSystem;

typedef struct WorkerCtx {
	JobSystem* js;
	uint32_t tid;
	HANDLE thread;

	uint64_t* residual;
	uint8_t* bad_bits;
	uint32_t  cap_win_len;

	uint32_t* off;     // carried offsets, [prime_count]
	uint32_t  off_cap;
} WorkerCtx;

static FORCEINLINE uint64_t load_u64(volatile LONG64* p) {
	return (uint64_t)InterlockedCompareExchange64((volatile LONG64*)p, 0, 0);
}

static FORCEINLINE void worker_epoch_done(JobSystem* js) {
	if (InterlockedDecrement(&js->epoch.active_workers) == 0) {
		SetEvent(js->epoch.evt_done);
	}
}

// Update best_m=min(best_m,m). If improved, shrink end_limit=min(end_limit, best_m-1).
static FORCEINLINE void try_set_best(JobSystem* js, uint64_t m) {
	for (;;) {
		uint64_t cur = load_u64(&js->epoch.best_m);
		if (m >= cur) return;

		if (InterlockedCompareExchange64(&js->epoch.best_m, (LONG64)m, (LONG64)cur) == (LONG64)cur) {
			uint64_t new_lim = (m == 0) ? 0 : (m - 1);
			for (;;) {
				uint64_t old_lim = load_u64(&js->epoch.end_limit);
				if (new_lim >= old_lim) break;
				if (InterlockedCompareExchange64(&js->epoch.end_limit, (LONG64)new_lim, (LONG64)old_lim) == (LONG64)old_lim)
					break;
			}
			return;
		}
	}
}

// ------------------------------------------------------------
// Worker buffer management
// ------------------------------------------------------------

static void ensure_worker_buffers(WorkerCtx* w, uint32_t win_len) {
	if (w->cap_win_len >= win_len) return;

	if (w->residual) { VirtualFree(w->residual, 0, MEM_RELEASE); w->residual = NULL; }
	if (w->bad_bits) { VirtualFree(w->bad_bits, 0, MEM_RELEASE); w->bad_bits = NULL; }

	size_t residual_bytes = (size_t)win_len * sizeof(uint64_t);
	size_t bad_bytes = (size_t)((win_len + 7u) >> 3);

	w->residual = (uint64_t*)VirtualAlloc(NULL, residual_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	w->bad_bits = (uint8_t*)VirtualAlloc(NULL, bad_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

	if (!w->residual || !w->bad_bits) {
		fprintf(stderr, "VirtualAlloc failed for worker buffers (win_len=%u)\n", win_len);
		ExitProcess(2);
	}
	w->cap_win_len = win_len;
}

static void ensure_worker_off(WorkerCtx* w, uint32_t prime_count) {
	if (prime_count == 0) {
		if (w->off) { VirtualFree(w->off, 0, MEM_RELEASE); w->off = NULL; }
		w->off_cap = 0;
		return;
	}
	if (w->off_cap >= prime_count) return;

	if (w->off) { VirtualFree(w->off, 0, MEM_RELEASE); w->off = NULL; }

	size_t bytes = (size_t)prime_count * sizeof(uint32_t);
	w->off = (uint32_t*)VirtualAlloc(NULL, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!w->off) {
		fprintf(stderr, "VirtualAlloc failed for worker off[] (count=%u)\n", prime_count);
		ExitProcess(2);
	}
	w->off_cap = prime_count;
}

// ------------------------------------------------------------
// Precompute per-k epoch arrays: fd[] and step_mod[]
// ------------------------------------------------------------

static void epoch_free_math(Epoch* e) {
	if (e->fd) { VirtualFree(e->fd, 0, MEM_RELEASE); e->fd = NULL; }
	if (e->step_mod) { VirtualFree(e->step_mod, 0, MEM_RELEASE); e->step_mod = NULL; }
}

static void epoch_prepare_math(JobSystem* js) {
	Epoch* e = &js->epoch;
	uint32_t n = e->primes.count;

	epoch_free_math(e);

	if (n == 0) {
		e->fd = NULL;
		e->step_mod = NULL;
		return;
	}

	size_t fd_bytes = (size_t)n * sizeof(FastDivU32);
	size_t sm_bytes = (size_t)n * sizeof(uint32_t);

	e->fd = (FastDivU32*)VirtualAlloc(NULL, fd_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	e->step_mod = (uint32_t*)VirtualAlloc(NULL, sm_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!e->fd || !e->step_mod) {
		fprintf(stderr, "VirtualAlloc failed for epoch fd/step_mod (count=%u)\n", n);
		ExitProcess(2);
	}

	for (uint32_t i = 0; i < n; ++i) {
		uint32_t p = e->primes.p[i];
		e->fd[i] = fastdiv_u32_make_prime(p);
	}

	for (uint32_t i = 0; i < n; ++i) {
		uint32_t p = e->primes.p[i];
		if (p == 2) e->step_mod[i] = (uint32_t)(e->step & 1ull);
		else        e->step_mod[i] = fastdiv_u32_mod(&e->fd[i], e->step);
	}
}

// ------------------------------------------------------------
// Sieve (carried offsets + FastDiv in factor stripping)
// ------------------------------------------------------------
//
// base_test = m0 + 1
// off[pi]   = smallest i>=0 such that (base_test + i) % p == 0, carried across tiles
//

static void sieve_window_bad_bits_carried_fastdiv(
	const Epoch* e,
	uint64_t base_test,
	uint32_t start_count,
	uint32_t* off,          // in/out [prime_count]
	uint64_t* residual,     // [win_len]
	uint8_t* bad_bits      // bitset [win_len]
) {
	uint32_t k = e->k;
	uint32_t win_len = start_count + k;

	for (uint32_t i = 0; i < win_len; ++i) residual[i] = base_test + (uint64_t)i;
	bitset_clear(bad_bits, win_len);

	uint32_t pc = e->primes.count;
	const uint32_t* primes = e->primes.p;
	const FastDivU32* fd = e->fd;
	const uint32_t* step_mod = e->step_mod;

	for (uint32_t pi = 0; pi < pc; ++pi) {
		uint32_t p = primes[pi];

		// process multiples inside this window
		for (uint32_t i = off[pi]; i < win_len; i += p) {
			uint64_t x = residual[i];

			if (p == 2) {
				// x is even here (by construction)
				x >>= (uint64_t)__builtin_ctzll(x);
			}
			else {
				const FastDivU32* f = &fd[pi];
				while (fastdiv_u32_divide_if_divisible(f, &x)) { /* repeat */ }
			}
			residual[i] = x;
		}

		// carry offset to next tile in this worker stream:
		// base_test' = base_test + step, so off' = off - (step % p) mod p
		uint32_t sm = step_mod[pi];
		if (sm) {
			uint32_t o = off[pi];
			off[pi] = (o >= sm) ? (o - sm) : (o + p - sm);
		}
	}

	// k-smooth iff residual == 1
	for (uint32_t i = 0; i < win_len; ++i) {
		if (residual[i] == 1) bitset_set(bad_bits, i);
	}
}

static uint64_t scan_tile_find_m_carried_fastdiv(
	const Epoch* e,
	uint64_t m0,
	uint32_t start_count,
	uint32_t* off,          // in/out (advanced by one tile)
	uint64_t* residual,
	uint8_t* bad_bits
) {
	if (start_count == 0) return UINT64_MAX;

	uint32_t k = e->k;
	uint32_t win_len = start_count + k;
	if (win_len < k) return UINT64_MAX;

	sieve_window_bad_bits_carried_fastdiv(e, m0 + 1, start_count, off, residual, bad_bits);

	uint32_t bad = 0;
	for (uint32_t i = 0; i < k; ++i) bad += bitset_get(bad_bits, i);
	if (bad == 0) return m0;

	for (uint32_t s = 1; s < start_count; ++s) {
		bad -= bitset_get(bad_bits, s - 1);
		bad += bitset_get(bad_bits, s + k - 1);
		if (bad == 0) return m0 + (uint64_t)s;
	}
	return UINT64_MAX;
}

// ------------------------------------------------------------
// Worker epoch init: initialize off[] for base_test0 once per epoch
// off[pi] = (p - (base_test0 % p)) % p, using FastDiv (no idiv), plus p==2 special.
// ------------------------------------------------------------

static void worker_init_offsets_for_epoch(WorkerCtx* w) {
	const Epoch* e = &w->js->epoch;
	uint32_t pc = e->primes.count;

	ensure_worker_off(w, pc);
	if (pc == 0) return;

	uint64_t base_test0 = e->start_m + (uint64_t)w->tid * (uint64_t)e->tile_len + 1;

	for (uint32_t pi = 0; pi < pc; ++pi) {
		uint32_t p = e->primes.p[pi];
		if (p == 2) {
			w->off[pi] = (uint32_t)(base_test0 & 1ull); // even => 0, odd => 1
		}
		else {
			uint32_t r = fastdiv_u32_mod(&e->fd[pi], base_test0);
			w->off[pi] = r ? (p - r) : 0u;
		}
	}
}

// ------------------------------------------------------------
// Worker thread (strided tiles)
// ------------------------------------------------------------

static DWORD WINAPI worker_main(void* p) {
	WorkerCtx* w = (WorkerCtx*)p;
	JobSystem* js = w->js;
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

	for (;;) {
		DWORD bytes = 0;
		ULONG_PTR key = 0;
		LPOVERLAPPED ov = NULL;
		GetQueuedCompletionStatus(js->iocp, &bytes, &key, &ov, INFINITE);

		if (key == KEY_STOP) break;

		if (key == KEY_START) {
			const Epoch* e = &js->epoch;

			uint64_t base = e->start_m + (uint64_t)w->tid * (uint64_t)e->tile_len;
			worker_init_offsets_for_epoch(w);

			for (;;) {
				uint64_t lim = load_u64(&js->epoch.end_limit);
				if (base > lim) {
					worker_epoch_done(js);
					break;
				}

				uint64_t max_starts = (lim - base + 1);
				uint32_t start_count = (max_starts >= e->tile_len) ? e->tile_len : (uint32_t)max_starts;

				uint32_t win_len = start_count + e->k;
				ensure_worker_buffers(w, win_len);

				uint64_t found = scan_tile_find_m_carried_fastdiv(e, base, start_count, w->off, w->residual, w->bad_bits);
				if (found != UINT64_MAX) {
					try_set_best(js, found);
				}

				base += e->step;
			}
		}
	}

	return 0;
}

// ------------------------------------------------------------
// Epoch orchestration (contiguous batch scan)
// ------------------------------------------------------------

static void epoch_begin(JobSystem* js, uint32_t k, uint64_t start_m, uint64_t end_m, uint32_t tile_len) {
	Epoch* e = &js->epoch;

	e->k = k;
	e->tile_len = tile_len;
	e->start_m = start_m;
	e->end_m = end_m;
	e->step = (uint64_t)tile_len * (uint64_t)js->thread_count;

	InterlockedExchange64(&e->best_m, (LONG64)UINT64_MAX);
	InterlockedExchange64(&e->end_limit, (LONG64)end_m);

	ResetEvent(e->evt_done);
	InterlockedExchange(&e->active_workers, (LONG)js->thread_count);

	for (uint32_t i = 0; i < js->thread_count; ++i)
		PostQueuedCompletionStatus(js->iocp, 0, KEY_START, NULL);
}

static uint64_t epoch_wait(JobSystem* js) {
	WaitForSingleObject(js->epoch.evt_done, INFINITE);
	return load_u64(&js->epoch.best_m);
}

static uint64_t safe_add_u64(uint64_t a, uint64_t b) {
	uint64_t c = a + b;
	return (c < a) ? UINT64_MAX : c;
}

// Find minimal m(k) by scanning contiguous batches; first batch with a solution yields global minimal.
static uint64_t find_m_for_k(JobSystem* js, uint32_t k, uint64_t start_m, uint32_t tile_len, uint64_t batch_tiles) {
	Epoch* e = &js->epoch;

	e->primes = primes_upto(k);

	// Precompute math arrays for this k (step depends on tile_len and thread_count).
	e->k = k;
	e->tile_len = tile_len;
	e->step = (uint64_t)tile_len * (uint64_t)js->thread_count;
	epoch_prepare_math(js);

	uint64_t cur = start_m;

	for (;;) {
		uint64_t span = (uint64_t)tile_len * batch_tiles;
		if (span == 0) span = tile_len;

		uint64_t end = safe_add_u64(cur, span - 1);
		epoch_begin(js, k, cur, end, tile_len);

		uint64_t best = epoch_wait(js);
		if (best != UINT64_MAX) {
			primes_free(&e->primes);
			epoch_free_math(e);
			return best;
		}

		cur = safe_add_u64(end, 1);
	}
}

// ------------------------------------------------------------
// Thread pool start/stop + waiting (supports >64 threads)
// ------------------------------------------------------------

static void wait_all_threads(HANDLE* th, uint32_t count) {
	// WaitForMultipleObjects max is 64; use chunking.
	const DWORD MAXW = MAXIMUM_WAIT_OBJECTS; // 64
	uint32_t i = 0;
	while (i < count) {
		DWORD n = (DWORD)((count - i) > MAXW ? MAXW : (count - i));
		WaitForMultipleObjects(n, th + i, TRUE, INFINITE);
		i += n;
	}
}

static uint32_t count_total_logical(void) {
	WORD groups = GetActiveProcessorGroupCount();
	uint32_t total = 0;
	for (WORD g = 0; g < groups; ++g) {
		DWORD c = GetActiveProcessorCount(g);
		total += (uint32_t)c;
	}
	return total;
}

static int start_workers(JobSystem* js, WorkerCtx* w, HANDLE* threads) {
	// If caller passed 0, you can set it to total logical here; else cap.
	uint32_t total = count_total_logical();
	if (js->thread_count == 0) js->thread_count = total;
	if (js->thread_count > total) js->thread_count = total;

	WORD groups = GetActiveProcessorGroupCount();
	uint32_t i = 0;

	for (WORD g = 0; g < groups && i < js->thread_count; ++g) {
		DWORD cores = GetActiveProcessorCount(g);

		// defensive: groups are max 64 LPs
		if (cores > 64) cores = 64;

		for (DWORD c = 0; c < cores && i < js->thread_count; ++c) {
			w[i].js = js;
			w[i].tid = i;

			HANDLE th = CreateThread(NULL, 0, worker_main, &w[i], CREATE_SUSPENDED, NULL);
			if (!th) return 0;

			GROUP_AFFINITY ga = { 0 };
			ga.Group = g;
			ga.Mask = (KAFFINITY)(1ull << c);

			if (!SetThreadGroupAffinity(th, &ga, NULL)) {
				CloseHandle(th);
				return 0;
			}

			// Optional: hint the scheduler (not required when hard-affinitized)
			// PROCESSOR_NUMBER pn = { .Group = g, .Number = (BYTE)c, .Reserved = 0 };
			// SetThreadIdealProcessorEx(th, &pn, NULL);

			ResumeThread(th);

			threads[i] = th;
			w[i].thread = th;
			++i;
		}
	}
	// If you want strict: require i == js->thread_count
	return (i == js->thread_count);
}

static void stop_workers(JobSystem* js) {
	for (uint32_t i = 0; i < js->thread_count; ++i)
		PostQueuedCompletionStatus(js->iocp, 0, KEY_STOP, NULL);
}

// ------------------------------------------------------------
// Main
// ------------------------------------------------------------

int main(int argc, char** argv) {
	setvbuf(stdout, NULL, _IONBF, 0);

	uint32_t K = 200;

	SYSTEM_INFO si;
	GetSystemInfo(&si);
	uint32_t threads = si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 8;

	uint32_t tile_len = 65536;
	uint64_t batch_tiles = 128;

	if (argc >= 2) K = (uint32_t)strtoul(argv[1], 0, 10);
	if (argc >= 3) threads = (uint32_t)strtoul(argv[2], 0, 10);
	if (argc >= 4) tile_len = (uint32_t)strtoul(argv[3], 0, 10);
	if (argc >= 5) batch_tiles = (uint64_t)_strtoui64(argv[4], 0, 10);

	SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);

	JobSystem js;
	memset(&js, 0, sizeof(js));
	js.thread_count = threads;

	js.iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, threads);
	if (!js.iocp) {
		fprintf(stderr, "CreateIoCompletionPort failed: %lu\n", GetLastError());
		return 1;
	}

	js.epoch.evt_done = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (!js.epoch.evt_done) {
		fprintf(stderr, "CreateEvent failed: %lu\n", GetLastError());
		return 1;
	}

	WorkerCtx* w = (WorkerCtx*)calloc(threads, sizeof(WorkerCtx));
	HANDLE* th = (HANDLE*)calloc(threads, sizeof(HANDLE));
	if (!w || !th) return 1;

	if (!start_workers(&js, w, th)) {
		fprintf(stderr, "Failed to start workers\n");
		return 1;
	}

	printf("; plateau points: k, m\n");

	uint64_t last_m = 0;
	uint64_t last_print = UINT64_MAX;

	for (uint32_t k = 1; k <= K; ++k) {
		uint64_t m = find_m_for_k(&js, k, last_m, tile_len, batch_tiles);
		last_m = m;

		if (m != last_print) {
			printf("%u, %llu\n", k, (unsigned long long)m);
			last_print = m;
		}
	}

	stop_workers(&js);
	wait_all_threads(th, threads);

	for (uint32_t i = 0; i < threads; ++i) {
		if (w[i].residual) VirtualFree(w[i].residual, 0, MEM_RELEASE);
		if (w[i].bad_bits) VirtualFree(w[i].bad_bits, 0, MEM_RELEASE);
		if (w[i].off)      VirtualFree(w[i].off, 0, MEM_RELEASE);
		CloseHandle(th[i]);
	}

	free(th);
	free(w);

	CloseHandle(js.epoch.evt_done);
	CloseHandle(js.iocp);
	return 0;
}
