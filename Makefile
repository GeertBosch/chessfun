CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall

mdcat: mdcat.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

TESTS := \
	tests/property-concat.sh \
	tests/property-width.sh \
	tests/property-blank-lines.sh

check: mdcat
	@fail=0; \
	for t in $(TESTS); do \
		name=$$(basename $$t .sh); \
		if out=$$(./$$t 2>&1); then \
			printf '✅ %s\n' "$$name"; \
		else \
			printf '❌ %s\n' "$$name"; \
			printf '%s\n' "$$out" | sed 's/^/    /'; \
			fail=1; \
		fi; \
	done; \
	exit $$fail

clean:
	rm -f mdcat

.PHONY: check clean
