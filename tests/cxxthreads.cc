// std::cout and std::fprintf from several threads (libstdc++ over APE's
// stdio, which locks each FILE once a second thread runs: tree
// sys/src/ape/lib/ap/stdio/flockfile.c). Each insertion is one whole
// line, written by fwrite under the lock: no line may come out torn.
// The output goes to a file; prints each failure and "cxxthreads: n failed".
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

static const int N = 8, LINES = 1000;

int
main()
{
	int failed = 0;

	if(std::freopen("/tmp/cxxthreads.out", "w", stdout) == nullptr){
		std::fprintf(stderr, "cxxthreads: FAIL freopen\n");
		return 1;
	}
	std::vector<std::thread> t;
	for(int i = 0; i < N; i++)
		t.emplace_back([i]{
			char line[80];
			std::snprintf(line, sizeof line, "cout %d abcdefghijklmnopqrstuvwxyz\n", i);
			for(int j = 0; j < LINES; j++)
				std::cout << line;
		});
	for(auto &x : t)
		x.join();
	std::cout.flush();
	std::fflush(stdout);
	std::fclose(stdout);

	std::ifstream in("/tmp/cxxthreads.out");
	std::string s;
	int n = 0, bad = 0;
	while(std::getline(in, s)){
		int k;
		char rest[64];
		n++;
		if(std::sscanf(s.c_str(), "cout %d %63s", &k, rest) != 2 || k < 0 || k >= N
		|| std::strcmp(rest, "abcdefghijklmnopqrstuvwxyz") != 0)
			bad++;
	}
	if(n != N * LINES || bad != 0){
		std::fprintf(stderr, "cxxthreads: FAIL %d lines (want %d), %d torn\n", n, N * LINES, bad);
		failed++;
	}
	std::remove("/tmp/cxxthreads.out");
	std::fprintf(stderr, "cxxthreads: %d failed\n", failed);
	return failed != 0;
}
