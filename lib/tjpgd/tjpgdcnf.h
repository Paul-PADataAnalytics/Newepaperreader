/*----------------------------------------------*/
/* TJpgDec System Configurations R0.03          */
/* (project configuration for e-reader covers)  */
/*----------------------------------------------*/

/* Keep our copy's symbols distinct from the TJpgDec build in the ESP32 ROM. */
#define jd_prepare cover_jd_prepare
#define jd_decomp cover_jd_decomp

#define JD_SZBUF        4096
/* Size of stream input buffer */

#define JD_FORMAT       2
/* Output pixel format: 2 = Grayscale (8-bit/pix) */

#define JD_USE_SCALE    1
/* Enables 1/2, 1/4 and 1/8 output descaling. */

#define JD_TBLCLIP      1
/* Table-based saturation (1 KB of code, a bit faster). */

#define JD_FASTDECODE   2
/* 32-bit barrel shifter plus huffman lookup tables. */

#define JD_DEFAULT_HUFFMAN 0
/* No default huffman tables (only needed for MJPEG frames). */
