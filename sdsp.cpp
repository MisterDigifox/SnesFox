#include "sdsp.hpp"

void Sdsp::reset() {
    m_regs.fill(0);
    m_addressLatch       = 0;
    m_lastDataPortByte   = 0;
}

uint8_t Sdsp::peekDataPort() const {
    return readReg(static_cast<int>(m_addressLatch) & 0x7F);
}

void Sdsp::pokeDataPort(uint8_t value) {
    writeReg(static_cast<int>(m_addressLatch) & 0x7F, value);
    m_lastDataPortByte = value;
}

uint8_t Sdsp::readReg(int addr) const {
    const int a = addr & 0x7F;
    return m_regs[static_cast<size_t>(a)];
}

void Sdsp::writeReg(int addr, uint8_t value) {
    const int a = addr & 0x7F;
    // ENDX: always clears on write (hardware), regardless of data.
    if (a == r_endx) {
        m_regs[static_cast<size_t>(r_endx)] = 0;
        return;
    }
    m_regs[static_cast<size_t>(a)] = value;
}

void Sdsp::runClocks(int /*dspClocks*/) {
    // Placeholder for BRR decode / voice mix / sampling at ~1024000 Hz.
}
