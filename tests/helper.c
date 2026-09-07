// Functions the test suite calls from compiled programs.

int ret3(void) {
    return 3;
}

int ret7(void) {
    return 7;
}

int add2(int a, int b) {
    return a + b;
}

int sub2(int a, int b) {
    return a - b;
}

int add6(int a, int b, int c, int d, int e, int f) {
    return a + b + c + d + e + f;
}

// Was rsp 16-byte aligned at the call site, as the ABI requires?
//
// An aligned call leaves rsp at 8 mod 16 on entry, since the call pushed the
// return address; the prologue's push rbp then makes rbp a multiple of 16.
int rsp_aligned(void) {
    return ((unsigned long)__builtin_frame_address(0) % 16) == 0;
}
