#pragma once

template<typename T>
T value2bit(T val) {
    CHECK((val & (val - 1)) == 0);
    CHECK(val != 0);
    T bit = 0;
    while (val > 1) {
        val = val >> 1;
        bit++;
    }
    return bit;
}

template<typename T>
T bit2value(T bit) {
    return (T) 1 << bit;
}
