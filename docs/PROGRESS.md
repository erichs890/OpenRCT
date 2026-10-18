# Progress

| Address | Name | Kind | Behaviour test | Byte match | Doc |
|---|---|---|---|---|---|
| 0x584F20 / 37 / 4E | file_seek_set/cur/end | VC6 C | ✅ 4000 seeks | pending (VC6) | [doc](functions/584F20_file_seek.md) |
| 0x584F65 | file_read | VC6 C | ✅ 201 cases | pending (VC6) | [doc](functions/584F65_file_read.md) |
| 0x585000 | file_write | VC6 C | ✅ 200 writes + invalid handle | pending (VC6) | [doc](functions/585000_file_write.md) |
| 0x419F53 | sawyer_read_byte | asm | ✅ 100 files, registers | n/a | [doc](functions/419F53_sawyer_read_byte.md) |
| 0x419FF6 | sawyer_rle_read_byte | asm | ✅ 600 streams | n/a | [doc](functions/419FF6_sawyer_rle_read_byte.md) |
| 0x419FEB | sawyer_rle_read | asm | ✅ 600 cases | n/a | [doc](functions/419FEB_sawyer_rle_read.md) |
| 0x419FB6 | sawyer_read_rewind | asm | ✅ 1288 calls | n/a | [doc](functions/419FB6_sawyer_read_rewind.md) |
| 0x419CC0 | sawyer_write_begin | asm | ✅ 400 sessions | n/a | [doc](functions/419CC0_sawyer_write_begin.md) |
| 0x419CEC | sawyer_write_byte | asm | ✅ 400 sessions | n/a | [doc](functions/419CEC_sawyer_write_byte.md) |
| 0x419DB1 | sawyer_rle_write_byte | asm | ✅ 400 sessions + round-trip | n/a | [doc](functions/419DB1_sawyer_rle_write_byte.md) |
| 0x419EDE | sawyer_rle_flush | asm | ✅ 400 sessions | n/a | [doc](functions/419EDE_sawyer_rle_flush.md) |
| 0x419D6B | sawyer_write_end | asm | ✅ 400 sessions | n/a | [doc](functions/419D6B_sawyer_write_end.md) |
| 0x419CB5 | sawyer_rle_write | asm | ✅ 400 sessions | n/a | [doc](functions/419CB5_sawyer_rle_write.md) |
| 0x41A05F | sawyer_write_checksum_sv4 | asm | ✅ 200 sessions | n/a | [doc](functions/41A05F_sawyer_write_checksum_sv4.md) |
| 0x41A0AB | sawyer_write_checksum_sc4 | asm | ✅ 200 sessions | n/a | [doc](functions/41A0AB_sawyer_write_checksum_sc4.md) |
| 0x41A0E4 | sawyer_check_sv4 | asm | ✅ 86 real + 300 synthetic files | n/a | [doc](functions/41A0E4_sawyer_check_sv4.md) |
| 0x41A215 | sawyer_check_sc4 | asm | ✅ 86 real + 300 synthetic files | n/a | [doc](functions/41A215_sawyer_check_sc4.md) |
| 0x40C3E1 / 0x40C860 | metal_supports_paint | asm | ✅ 6000 cases, whole-memory diff | n/a | [doc](functions/40C3E1_metal_supports_paint.md) |
| 0x40BC98 / 0x40C038 | wooden_supports_paint | asm | ✅ 6000 cases, whole-memory diff | n/a | [doc](functions/40BC98_wooden_supports_paint.md) |
| 0x57FAB6 | gfx_invalidate_rect | VC6 C | ✅ 20000 cases (+ bound fix) | pending (VC6) | [doc](functions/57FAB6_gfx_invalidate_rect.md) |
| 0x425E26 | viewport_render | asm | ✅ 20000 cases | n/a | [doc](functions/425E26_viewport_render.md) |
| 0x4288AF / 0x4288E9 | window_zoom_in / out | asm | ✅ 20000 cases | n/a | [doc](functions/4288AF_window_zoom.md) |
| 0x4263B8 / 0x42653B / 0x4266CC / 0x42685F | paint_add_parent (4 rotations) | asm | ✅ 20000 cases, registers exact | n/a | [doc](functions/4263B8_paint_add_parent.md) |
| 0x426A04 / 0x426BA8 / 0x426D5D / 0x426F17 | paint_add_child (4 rotations) | asm | ✅ 20000 cases, registers exact | n/a | [doc](functions/426A04_paint_add_child.md) |
| 0x4270E0 / 0x42723E / 0x4273A8 / 0x427514 | paint_add_attached (4 rotations) | asm | ✅ 20000 cases, registers exact | n/a | [doc](functions/4270E0_paint_add_attached.md) |
| 0x42635F | paint_session_begin | asm | ✅ 2000 cases, registers exact | n/a | [doc](functions/42635F_paint_list.md) |
| 0x42839E | paint_arrange | asm | ✅ 20000 cases, registers exact | n/a | [doc](functions/42635F_paint_list.md) |
| 0x42860C | paint_draw | asm | ✅ 20000 cases (sprite call sequence) | n/a | [doc](functions/42635F_paint_list.md) |

Totals: 42 / 16 360 functions (by call sites, the 4 most-called functions in the game are done). The SV4/SC4 stream module (0x419CB5–0x41A32F) is complete.
