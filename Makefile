CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall

mdcat: mdcat.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

check: mdcat
	./tests/property-concat.sh
	./tests/property-width.sh
	./tests/property-blank-lines.sh

clean:
	rm -f mdcat

.PHONY: check clean
