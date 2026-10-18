int helper(int value);

struct Widget;

template <typename T> int width_of(const Widget& widget, T) { return widget.width + helper(5); }

struct Widget { int width; };

int lenient_entry() { return helper(4); }
