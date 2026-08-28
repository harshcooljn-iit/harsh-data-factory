# Benchmark-style tests. Labelled "performance" so they can be run on their
# own (ctest -L performance) and kept out of the fast unit lane. They print
# real measured numbers and only assert loose upper bounds to stay CI-stable.
flowforge_add_test(NAME bench_dag SOURCES bench_dag.cpp LABELS performance)
flowforge_add_test(NAME bench_scheduler SOURCES bench_scheduler.cpp LABELS performance)
