
CXXFLAGS=-Ofast -g -pthread

onitama: onitama.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

