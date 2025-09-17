// mpiexec -np ${NRANKS} -ppn ${RANKS_PER_NODE} --cpu-bind ${CPU_BINDING1}  --no-vni -genvall ./in-bi-bw injection --size 1m --iters 50
// mpiexec -np ${NRANKS} -ppn ${RANKS_PER_NODE} --cpu-bind ${CPU_BINDING1}  --no-vni -genvall ./in-bi-bw bisection --size 512k --iters 200
// mpiexec -np ${NRANKS} -ppn ${RANKS_PER_NODE} --cpu-bind ${CPU_BINDING1}  --no-vni -genvall ./in-bi-bw injection --size 2g --iters 10
// mpiexec -np ${NRANKS} -ppn ${RANKS_PER_NODE} --cpu-bind ${CPU_BINDING1}  --no-vni -genvall ./in-bi-bw-2 injection --size 10g --iters 200 


#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define DEFAULT_MSG_SIZE (1<<20)   // 1 MB
#define DEFAULT_NITERS   100

// --------------------------------------------------
// Helpers
// --------------------------------------------------
size_t parse_size(const char *arg) {
    char *end;
    double val = strtod(arg, &end);
    size_t mult = 1;

    if (*end != '\0') {
        switch (tolower(*end)) {
            case 'k': mult = 1UL << 10; break;
            case 'm': mult = 1UL << 20; break;
            case 'g': mult = 1UL << 30; break;
            default:
                fprintf(stderr, "Unknown size suffix '%c' in %s\n", *end, arg);
                MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }
    return (size_t)(val * mult);
}

int parse_iters(const char *arg) {
    return atoi(arg);
}

// --------------------------------------------------
// Injection Bandwidth Test
// --------------------------------------------------
double run_injection_test(int rank, int nprocs, char *sendbuf, char *recvbuf, int niters, size_t msg_size) {
    MPI_Barrier(MPI_COMM_WORLD);

    double t_start = MPI_Wtime();
    for (int it = 0; it < niters; it++) {
        for (int peer = 0; peer < nprocs; peer++) {
            if (peer == rank) continue;
            MPI_Sendrecv(sendbuf, msg_size, MPI_CHAR, peer, 0,
                         recvbuf, msg_size, MPI_CHAR, peer, 0,
                         MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
    }
    double t_end = MPI_Wtime();

    double local_bytes = (double)(nprocs - 1) * msg_size * niters * 2;
    double local_bw = (local_bytes / (t_end - t_start)) / 1e9;

    printf("Rank %d injected %.2f GB/s\n", rank, local_bw);

    double global_bw;
    MPI_Reduce(&local_bw, &global_bw, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        printf("Measured GLOBAL injection bandwidth = %.2f GB/s\n", global_bw);
    }

    return local_bw;
}

// --------------------------------------------------
// Bisection Bandwidth Test
// --------------------------------------------------
double run_bisection_test(int rank, int nprocs, char *sendbuf, char *recvbuf, int niters, size_t msg_size) {
    int half = nprocs / 2;
    int partner = (rank < half) ? rank + half : rank - half;

    MPI_Barrier(MPI_COMM_WORLD);

    double t_start = MPI_Wtime();
    for (int it = 0; it < niters; it++) {
        MPI_Sendrecv(sendbuf, msg_size, MPI_CHAR, partner, 0,
                     recvbuf, msg_size, MPI_CHAR, partner, 0,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }
    double t_end = MPI_Wtime();

    double bis_local_bytes = (double)msg_size * niters * 2;
    double bis_local_bw = (bis_local_bytes / (t_end - t_start)) / 1e9;

    printf("Rank %d bisection BW = %.2f GB/s\n", rank, bis_local_bw);

    double bis_total_bw;
    if (rank < half) {
        MPI_Reduce(&bis_local_bw, &bis_total_bw, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    } else {
        MPI_Reduce(&bis_local_bw, NULL, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    }

    if (rank == 0) {
        printf("Measured BISECTION bandwidth = %.2f GB/s\n", bis_total_bw);
    }

    return bis_local_bw;
}

// --------------------------------------------------
// Main
// --------------------------------------------------
int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);

    int rank, nprocs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    size_t msg_size = DEFAULT_MSG_SIZE;
    int niters = DEFAULT_NITERS;

    // Parse args
    const char *mode = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--size") == 0 && i+1 < argc) {
            msg_size = parse_size(argv[++i]);
        } else if (strcmp(argv[i], "--iters") == 0 && i+1 < argc) {
            niters = parse_iters(argv[++i]);
        } else if (strcmp(argv[i], "injection") == 0 || strcmp(argv[i], "bisection") == 0) {
            mode = argv[i];
        }
    }

    if (!mode) {
        if (rank == 0) {
            printf("Usage: %s [injection|bisection] [--size <N[k|m|g]>] [--iters N]\n", argv[0]);
        }
        MPI_Finalize();
        return 1;
    }

    // Buffers
    char *sendbuf = malloc(msg_size);
    char *recvbuf = malloc(msg_size);
    memset(sendbuf, 1, msg_size);
    memset(recvbuf, 0, msg_size);

    if (strcmp(mode, "injection") == 0) {
        run_injection_test(rank, nprocs, sendbuf, recvbuf, niters, msg_size);
    } else {
        run_bisection_test(rank, nprocs, sendbuf, recvbuf, niters, msg_size);
    }

    free(sendbuf);
    free(recvbuf);
    MPI_Finalize();
    return 0;
}
