"""Inspect the emitted x64 call that caused the City profile crash."""
import pathlib
import struct
import sys

import capstone

payload = pathlib.Path(sys.argv[1]).read_bytes()
pe = struct.unpack_from('<I', payload, 60)[0]
section_count = struct.unpack_from('<H', payload, pe + 6)[0]
optional_size = struct.unpack_from('<H', payload, pe + 20)[0]
image_base = struct.unpack_from('<Q', payload, pe + 24 + 24)[0]
decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
hits = []
for section in range(section_count):
    header = pe + 24 + optional_size + section * 40
    _, rva, size, raw = struct.unpack_from('<IIII', payload, header + 8)
    flags = struct.unpack_from('<I', payload, header + 36)[0]
    if not flags & 0x20000000:
        continue
    code = payload[raw:raw + size]
    cursor = 0
    while (cursor := code.find(bytes.fromhex('480550352c00'), cursor)) != -1:
        start = cursor - 11
        instructions = list(decoder.disasm(code[start:cursor + 8], image_base + rva + start))
        assert instructions[0].mnemonic == 'movzx' and instructions[0].op_str.startswith('ecx, byte ptr '), (
            'Profile slot is not zero extended to the native 32-bit argument', instructions[0].op_str)
        assert instructions[1].mnemonic == 'mov' and instructions[1].op_str.startswith('rax, qword ptr ')
        assert instructions[2].mnemonic == 'add' and instructions[2].op_str == 'rax, 0x2c3550'
        assert instructions[3].mnemonic == 'call' and instructions[3].op_str == 'rax'
        hits.append(instructions)
        cursor += 6
assert len(hits) == 1, f'Expected one native shock getter call, found {len(hits)}'
for instruction in hits[0]:
    print(f'{instruction.address:#x}: {instruction.mnemonic} {instruction.op_str}')
print('PASS: native profile getter receives a zero-extended 32-bit slot')
