/* Reserves 0x401000..0xC50000 inside the harness image itself, before any
   heap or stack can land there; the Makefile places it with --section-start. Used by every exe that loads RCT.
   No file bytes ("b"); writable and executable ("wx"), as image pages cannot gain X later. */
.section .rctimg,"bwx"
.globl _rct_image_space
_rct_image_space:
.space 0x84F000
