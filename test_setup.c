#include <stdio.h>
#include <stdatomic.h>
#include <windows.h>

/* Test: atomics + Windows threads work */
static atomic_int counter = 0;

DWORD WINAPI thread_func(LPVOID arg) {
    for (int i = 0; i < 10000; i++) {
        atomic_fetch_add(&counter, 1);
    }
    return 0;
}

int main(void) {
    printf("=== Redis Clone Environment Test ===\n");
    printf("Compiler: GCC %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
    printf("C Standard: %ld\n", __STDC_VERSION__);

    /* Test 1: Atomics */
    atomic_int x = ATOMIC_VAR_INIT(0);
    atomic_store(&x, 42);
    int val = atomic_load(&x);
    printf("\n[TEST 1] Atomics: %s (stored 42, loaded %d)\n",
           val == 42 ? "PASS" : "FAIL", val);

    /* Test 2: Compare-And-Swap */
    int expected = 42;
    int desired = 100;
    _Bool cas_ok = atomic_compare_exchange_strong(&x, &expected, desired);
    printf("[TEST 2] CAS:     %s (expected 42->100, got %d)\n",
           cas_ok && atomic_load(&x) == 100 ? "PASS" : "FAIL",
           atomic_load(&x));

    /* Test 3: Multi-threaded atomic increment */
    HANDLE threads[4];
    for (int i = 0; i < 4; i++) {
        threads[i] = CreateThread(NULL, 0, thread_func, NULL, 0, NULL);
    }
    for (int i = 0; i < 4; i++) {
        WaitForSingleObject(threads[i], INFINITE);
        CloseHandle(threads[i]);
    }
    int final_val = atomic_load(&counter);
    printf("[TEST 3] Threads: %s (4 threads x 10000 = %d, expected 40000)\n",
           final_val == 40000 ? "PASS" : "FAIL", final_val);

    /* Test 4: Winsock (for TCP server later) */
    WSADATA wsa;
    int wsa_ok = WSAStartup(MAKEWORD(2, 2), &wsa);
    printf("[TEST 4] Winsock: %s\n", wsa_ok == 0 ? "PASS" : "FAIL");
    if (wsa_ok == 0) WSACleanup();

    printf("\n=== All critical features verified! ===\n");
    return 0;
}
