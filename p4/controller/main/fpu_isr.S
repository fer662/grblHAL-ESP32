/* SPDX-License-Identifier: GPL-3.0-or-later
 * P4 RV32F interrupt boundary. No RTOS lazy-FPU exception may occur inside it.
 * Save the entire physical register file, FCSR and previous mstatus.FS state;
 * the RTOS retains ownership of the interrupted task's original context.
 * Stack frame is 16-byte aligned. Never enable interrupts or alter other CSRs.
 */
#include "sdkconfig.h"
#if !CONFIG_IDF_TARGET_ESP32P4
#error "This interrupt boundary is specific to ESP32-P4 RV32F."
#endif

.section .iram1,"ax",@progbits
.align 2
.macro save_fp
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    fsw f\n, (\n * 4)(sp)
    .endr
    frcsr t0
    sw t0, 128(sp)
.endm
.macro restore_fp
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    flw f\n, (\n * 4)(sp)
    .endr
    lw t0, 128(sp)
    fscsr t0
.endm
.global h5_fpu_call
.type h5_fpu_call,@function
h5_fpu_call:
    addi sp, sp, -144
    sw ra, 140(sp)
    li t1, 0x2000
    csrrs t0, mstatus, t1
    li t1, 0x6000
    and t0, t0, t1
    sw t0, 132(sp)
    save_fp
    fscsr zero /* nearest rounding, no inherited task exception flags */
    jalr ra, a0, 0
    restore_fp
    li t1, 0x6000
    csrc mstatus, t1
    lw t0, 132(sp)
    csrs mstatus, t0
    lw ra, 140(sp)
    addi sp, sp, 144
    ret
.size h5_fpu_call, .-h5_fpu_call

/* Bench ISR payload: deliberately clobber every FP register and FCSR. */
.global h5_fpu_clobber
.type h5_fpu_clobber,@function
h5_fpu_clobber:
    li t0, 0x40000000
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    fmv.w.x f\n, t0
    .endr
    fdiv.s f0, f0, f1
    li t0, 0x1f
    fscsr t0
    ret
.size h5_fpu_clobber, .-h5_fpu_clobber

/* Task-side canary: keep all 32 FP registers and FCSR live across 1000 IRQs.
 * a0 points to the IRQ count. A one-second cycle deadline prevents hangs.
 * The first FP instruction may use normal task-context lazy FPU allocation.
 */
.global h5_fpu_canary
.type h5_fpu_canary,@function
h5_fpu_canary:
    addi sp, sp, -144
    save_fp
    li t0, 0x3f800000
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    fmv.w.x f\n, t0
    addi t0, t0, 1
    .endr
    li t0, 0x60
    fscsr t0
    rdcycle t3
1:
    li t0, 0x3f800000
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    fmv.x.w t1, f\n
    bne t0, t1, 2f
    addi t0, t0, 1
    .endr
    frcsr t0
    li t1, 0x60
    bne t0, t1, 2f
    lw t0, 0(a0)
    li t1, 1000
    bgeu t0, t1, 3f
    rdcycle t0
    sub t0, t0, t3
    li t1, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000
    bltu t0, t1, 1b
2:
    li a0, 0
    j 4f
3:
    li a0, 1
4:
    restore_fp
    addi sp, sp, 144
    ret
.size h5_fpu_canary, .-h5_fpu_canary
