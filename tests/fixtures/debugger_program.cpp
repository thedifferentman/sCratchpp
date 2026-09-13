volatile int result;

int recurse(int n) {
    int value = n; // DEBUG_RECURSE
    if (n > 0) value += recurse(n - 1);
    return value; // DEBUG_RETURN
}

int main() {
    int sum = 0; // DEBUG_ENTRY
    for (int i = 0; i < 3; ++i) {
        sum += i; // DEBUG_LOOP
    }
    sum += recurse(3); // DEBUG_CALL
    result = sum; // DEBUG_AFTER_CALL
    return result == 9 ? 0 : 1; // DEBUG_END
}
