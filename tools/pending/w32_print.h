/* w32_print.h - declarations src/print.c needs that are not in w32.h yet (merge into w32.h; 32-bit sdk values) */
API HGLOBAL WINAPI GlobalFree(HGLOBAL);                 /* kernel32 */
#ifndef IDC_WAIT
#define IDC_WAIT MAKEINTRESOURCEW(32514)
#endif
#define PD_PAGENUMS 0x00000002
#define PDERR_NODEFAULTPRN 0x1008                       /* cderr.h */
#define PDERR_DNDMMISMATCH 0x1009
#define PDERR_PRINTERNOTFOUND 0x100B
#define PDERR_DEFAULTDIFFERENT 0x100C
#define ERROR_PRINT_CANCELLED 63L                       /* winerror.h */
#define ERROR_CANCELLED 1223L
