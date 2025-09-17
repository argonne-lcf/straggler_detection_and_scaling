# straggler_detection_and_scaling

Some handy tools to detect stragglers and bad nodes when running applications at scale

## Hello World at scale

A simple echo hello world at 10k nodes to filter out any bad nodes

## Hostfile sort filter nodes

A simple python tool to filter a node from a hostfile or sort the hostfile

## Injection Bisection tests

A simple injection and bisection bandwidth test to filter out unreachable hosts.

## I/O Peak tests

A simple IOR to measure peak I/O bandwidth

## Compute kernals and other benchmarks

Frequently used compute kernals to get any straggler GPU in compute performance. - Need to a job script and flops counter per node or gpu as log messages.

## Data loading and Movers at scale

https://github.com/argonne-lcf/scalable_conda_env to copy a tar file to /tmp on all compute nodes

https://docs.alcf.anl.gov/aurora/data-management/copper/copper/ for python imports at scale

## Rerun prolog again after the job start 

/pe/pbs/scripts/epilogue.sh  or from /pe/pbs/scripts/ directory

## Pytorch and collective communication test

https://github.com/argonne-lcf/DLcomm_benchmark/tree/master/examples/1_simple_flat
