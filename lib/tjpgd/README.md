# TJpgDec

Tiny JPEG Decompressor R0.03 by ChaN (http://elm-chan.org/fsw/tjpgd/),
taken from Espressif's `esp_jpeg` component (idf-extra-components 1.3.1).
`tjpgd.c` and `tjpgd.h` are unmodified; `tjpgdcnf.h` is this project's
configuration (grayscale output with 1/2..1/8 scaling, used to decode
EPUB cover images without holding the full-size picture in memory).

License (from the source header): free software, no warranty, no
restriction on use; redistributions of source code must retain the
copyright notice.
