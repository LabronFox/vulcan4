# scratch.s - a trivial PlayStation 2 (MIPS R5900) program.
#
# THIS IS OURS. It is not from any game disc. It exists purely to prove that
# PS2Recomp's R5900 -> C++ path works on Linux before Gran Turismo 4's own
# executable (goal G0.2) ever touches the toolchain.
#
# Build:
#   mips-linux-gnu-as -march=5900 -mabi=32 -o scratch.o scratch.s
#   mips-linux-gnu-ld -Ttext=0x00100000 -e vulcan_entry -o scratch.elf scratch.o
#
# The logic is deliberately trivial but *specific*, so the generated C++ can be
# read side by side against these lines (goal G0.3's discipline, rehearsed here):
#
#   vulcan_add:   returns (a0 + 100) + a1
#   vulcan_main:  calls vulcan_add(3, 4), stores the result to 0x00200000

	.set	noreorder
	.set	nomacro
	.abicalls_off = 0

	.text

#---------------------------------------------------------------------
# uint32_t vulcan_add(uint32_t a0, uint32_t a1)
#   t0 = a0 + 100
#   v0 = t0 + a1
#---------------------------------------------------------------------
	.globl	vulcan_add
	.ent	vulcan_add
	.type	vulcan_add, @function
vulcan_add:
	addiu	$t0, $a0, 100		# 0x24080064  addiu $t0, $a0, 100
	addu	$v0, $t0, $a1		# 0x010A5821  addu  $v0, $t0, $a1
	jr	$ra			# 0x03E00008  jr    $ra
	nop				# 0x00000000  delay slot
	.end	vulcan_add
	.size	vulcan_add, . - vulcan_add

#---------------------------------------------------------------------
# void vulcan_entry(void)
#   v0 = vulcan_add(3, 4)          -> 107
#   store v0 at 0x00200000
#---------------------------------------------------------------------
	.globl	vulcan_entry
	.ent	vulcan_entry
	.type	vulcan_entry, @function
vulcan_entry:
	li	$a0, 3			# 0x24040003  addiu $a0, $zero, 3
	li	$a1, 4			# 0x24050004  addiu $a1, $zero, 4
	jal	vulcan_add		# 0x0C000000 + target
	nop				# 0x00000000  delay slot
	lui	$t0, 0x0020		# 0x3C080020  lui   $t0, 0x0020
	sw	$v0, 0x0000($t0)		# 0xAD020000  sw    $v0, 0($t0)
	jr	$ra			# 0x03E00008  jr    $ra
	nop				# 0x00000000  delay slot
	.end	vulcan_entry
	.size	vulcan_entry, . - vulcan_entry

#---------------------------------------------------------------------
# The write target: vulcan_entry stores its result here.
#---------------------------------------------------------------------
	.globl	vulcan_result
	.section .data
	.align	2
	.type	vulcan_result, @object
	.size	vulcan_result, 4
vulcan_result:
	.word	0			# overwritten at runtime with 107
