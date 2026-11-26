#ifndef TUITUBE_TT_KEYSYMS_H
#define TUITUBE_TT_KEYSYMS_H

#ifndef _WIN32
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#else

#include <stdint.h>

typedef uint32_t xkb_keysym_t;

#define XKB_KEY_NoSymbol         0x000000u

/* ASCII printable keysyms */
#define XKB_KEY_space            0x0020u
#define XKB_KEY_apostrophe       0x0027u
#define XKB_KEY_comma            0x002cu
#define XKB_KEY_minus            0x002du
#define XKB_KEY_period           0x002eu
#define XKB_KEY_slash            0x002fu
#define XKB_KEY_0                0x0030u
#define XKB_KEY_1                0x0031u
#define XKB_KEY_2                0x0032u
#define XKB_KEY_3                0x0033u
#define XKB_KEY_4                0x0034u
#define XKB_KEY_5                0x0035u
#define XKB_KEY_6                0x0036u
#define XKB_KEY_7                0x0037u
#define XKB_KEY_8                0x0038u
#define XKB_KEY_9                0x0039u
#define XKB_KEY_semicolon        0x003bu
#define XKB_KEY_equal            0x003du
#define XKB_KEY_question         0x003fu
#define XKB_KEY_A                0x0041u
#define XKB_KEY_B                0x0042u
#define XKB_KEY_C                0x0043u
#define XKB_KEY_D                0x0044u
#define XKB_KEY_E                0x0045u
#define XKB_KEY_F                0x0046u
#define XKB_KEY_G                0x0047u
#define XKB_KEY_H                0x0048u
#define XKB_KEY_I                0x0049u
#define XKB_KEY_J                0x004au
#define XKB_KEY_K                0x004bu
#define XKB_KEY_L                0x004cu
#define XKB_KEY_M                0x004du
#define XKB_KEY_N                0x004eu
#define XKB_KEY_O                0x004fu
#define XKB_KEY_P                0x0050u
#define XKB_KEY_Q                0x0051u
#define XKB_KEY_R                0x0052u
#define XKB_KEY_S                0x0053u
#define XKB_KEY_T                0x0054u
#define XKB_KEY_U                0x0055u
#define XKB_KEY_V                0x0056u
#define XKB_KEY_W                0x0057u
#define XKB_KEY_X                0x0058u
#define XKB_KEY_Y                0x0059u
#define XKB_KEY_Z                0x005au
#define XKB_KEY_bracketleft      0x005bu
#define XKB_KEY_backslash        0x005cu
#define XKB_KEY_bracketright     0x005du
#define XKB_KEY_grave            0x0060u
#define XKB_KEY_a                0x0061u
#define XKB_KEY_b                0x0062u
#define XKB_KEY_c                0x0063u
#define XKB_KEY_d                0x0064u
#define XKB_KEY_e                0x0065u
#define XKB_KEY_f                0x0066u
#define XKB_KEY_g                0x0067u
#define XKB_KEY_h                0x0068u
#define XKB_KEY_i                0x0069u
#define XKB_KEY_j                0x006au
#define XKB_KEY_k                0x006bu
#define XKB_KEY_l                0x006cu
#define XKB_KEY_m                0x006du
#define XKB_KEY_n                0x006eu
#define XKB_KEY_o                0x006fu
#define XKB_KEY_p                0x0070u
#define XKB_KEY_q                0x0071u
#define XKB_KEY_r                0x0072u
#define XKB_KEY_s                0x0073u
#define XKB_KEY_t                0x0074u
#define XKB_KEY_u                0x0075u
#define XKB_KEY_v                0x0076u
#define XKB_KEY_w                0x0077u
#define XKB_KEY_x                0x0078u
#define XKB_KEY_y                0x0079u
#define XKB_KEY_z                0x007au

/* Editing & navigation keysyms */
#define XKB_KEY_BackSpace        0xff08u
#define XKB_KEY_Tab              0xff09u
#define XKB_KEY_Return           0xff0du
#define XKB_KEY_Escape           0xff1bu
#define XKB_KEY_Delete           0xffffu
#define XKB_KEY_Home             0xff50u
#define XKB_KEY_Left             0xff51u
#define XKB_KEY_Up               0xff52u
#define XKB_KEY_Right            0xff53u
#define XKB_KEY_Down             0xff54u
#define XKB_KEY_Page_Up          0xff55u
#define XKB_KEY_Page_Down        0xff56u
#define XKB_KEY_End              0xff57u
#define XKB_KEY_Insert           0xff63u
#define XKB_KEY_Num_Lock         0xff7fu
#define XKB_KEY_ISO_Level3_Shift 0xfe03u
#define XKB_KEY_ISO_Left_Tab     0xfe20u

/* Keypad keysyms */
#define XKB_KEY_KP_Enter         0xff8du
#define XKB_KEY_KP_Home          0xff95u
#define XKB_KEY_KP_Left          0xff96u
#define XKB_KEY_KP_Up            0xff97u
#define XKB_KEY_KP_Right         0xff98u
#define XKB_KEY_KP_Down          0xff99u
#define XKB_KEY_KP_Page_Up       0xff9au
#define XKB_KEY_KP_Page_Down     0xff9bu
#define XKB_KEY_KP_End           0xff9cu
#define XKB_KEY_KP_Begin         0xff9du
#define XKB_KEY_KP_Insert        0xff9eu
#define XKB_KEY_KP_Delete        0xff9fu

/* Function keysyms */
#define XKB_KEY_F1               0xffbeu
#define XKB_KEY_F2               0xffbfu
#define XKB_KEY_F3               0xffc0u
#define XKB_KEY_F4               0xffc1u
#define XKB_KEY_F5               0xffc2u
#define XKB_KEY_F6               0xffc3u
#define XKB_KEY_F7               0xffc4u
#define XKB_KEY_F8               0xffc5u
#define XKB_KEY_F9               0xffc6u
#define XKB_KEY_F10              0xffc7u
#define XKB_KEY_F11              0xffc8u
#define XKB_KEY_F12              0xffc9u

/* Modifier keysyms */
#define XKB_KEY_Shift_L          0xffe1u
#define XKB_KEY_Shift_R          0xffe2u
#define XKB_KEY_Control_L        0xffe3u
#define XKB_KEY_Control_R        0xffe4u
#define XKB_KEY_Caps_Lock        0xffe5u
#define XKB_KEY_Meta_L           0xffe7u
#define XKB_KEY_Meta_R           0xffe8u
#define XKB_KEY_Alt_L            0xffe9u
#define XKB_KEY_Alt_R            0xffeau
#define XKB_KEY_Super_L          0xffebu
#define XKB_KEY_Super_R          0xffecu

#endif /* _WIN32 */

#endif /* TUITUBE_TT_KEYSYMS_H */
