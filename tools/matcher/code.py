"""One function's instructions, in a form two builds can be compared on:
absolute addresses replaced, so a recompiled but unchanged function reads the
same."""
import difflib
import re

import capstone
from capstone import x86

_cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
_cs.detail = True

# Registers whose displacements are stack slots, not object members. MSVC
# addresses the frame through R11 in epilogues (lea r11,[rsp+N]).
_FRAME_REGS = {x86.X86_REG_RSP, x86.X86_REG_RBP, x86.X86_REG_RIP, x86.X86_REG_R11}
_HEX = re.compile(r'0x[0-9a-f]+')
_MAX_LEAF = 0x1000


class Function:
    def __init__(self, pe, start):
        self.pe, self.start = pe, start
        end = dict(pe.functions()).get(start)
        self.end = end if end is not None else self._leaf_end()
        self.size = self.end - self.start
        self.insns = list(_cs.disasm(pe.read(start, self.size), start))

    def _leaf_end(self):
        # No unwind data: the function is a leaf. It ends at the last
        # instruction before int3 padding or the next function.
        funcs = self.pe.functions()
        limit = min((s for s, _ in funcs if s > self.start), default=self.start + _MAX_LEAF)
        limit = min(limit, self.start + _MAX_LEAF)
        end = self.start
        for insn in _cs.disasm(self.pe.read(self.start, limit - self.start), self.start):
            if insn.mnemonic == 'int3':
                break
            end = insn.address + insn.size
        return end

    def _branch_target(self, insn):
        if insn.group(capstone.CS_GRP_JUMP) or insn.group(capstone.CS_GRP_CALL):
            op = insn.operands[0] if insn.operands else None
            if op is not None and op.type == x86.X86_OP_IMM:
                return op.imm
        return None

    def _rip_target(self, insn):
        for op in insn.operands:
            if op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_RIP:
                return insn.address + insn.size + op.mem.disp
        return None

    def calls(self):
        return [t for i in self.insns if i.group(capstone.CS_GRP_CALL)
                for t in [self._branch_target(i)] if t is not None]

    def import_calls(self):
        imports = self.pe.imports()
        out = set()
        for i in self.insns:
            if i.group(capstone.CS_GRP_CALL) or i.mnemonic == 'jmp':
                slot = self._rip_target(i)
                if slot in imports:
                    out.add(imports[slot])
        return out

    def rip_refs(self):
        return [t for i in self.insns for t in [self._rip_target(i)] if t is not None]

    def member_offsets(self):
        return {op.mem.disp for i in self.insns for op in i.operands
                if op.type == x86.X86_OP_MEM and op.mem.base not in _FRAME_REGS
                and op.mem.base != x86.X86_REG_INVALID and op.mem.disp}

    def normalised(self):
        out = []
        for i in self.insns:
            if self._branch_target(i) is not None:
                ops = 'ADDR'
            elif self._rip_target(i) is not None:
                ops = re.sub(r'\[rip [+-] 0x[0-9a-f]+\]', '[ADDR]', i.op_str)
            else:
                ops = i.op_str
            out.append(f'{i.mnemonic} {ops}'.strip())
        return out

    @property
    def block_count(self):
        leaders = {self.start}
        for i in self.insns:
            t = self._branch_target(i)
            if i.group(capstone.CS_GRP_JUMP) or i.group(capstone.CS_GRP_RET):
                leaders.add(i.address + i.size)
                if t is not None and self.start <= t < self.end:
                    leaders.add(t)
        return len({a for a in leaders if a < self.end})


def similarity(a, b):
    return difflib.SequenceMatcher(None, a.normalised(), b.normalised(), autojunk=False).ratio()
