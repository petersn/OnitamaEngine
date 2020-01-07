
CXXFLAGS=-Ofast -g

onitama: onitama.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

