#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define DEFAULT_MSG_SIZE (1<<20)
#define DEFAULT_NITERS 100

// ------------------ Helpers ------------------
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

// ------------------ Injection Test (Deadlock-Safe) ------------------
void run_injection_test(int rank, int nprocs, int local_rank, int local_size, MPI_Comm nodecomm,
                             char *buf, int niters, size_t msg_size,
                             double *global_inj_oneway, double *global_inj_bidirectional) {

    MPI_Request *reqs_send = malloc((nprocs-1) * sizeof(MPI_Request));
    MPI_Request *reqs_recv = malloc((nprocs-1) * sizeof(MPI_Request));
    char **recvbufs = malloc((nprocs-1) * sizeof(char*));

    // allocate separate recv buffer per peer to avoid overwriting
    for (int i = 0; i < nprocs-1; i++) {
        recvbufs[i] = malloc(msg_size);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    double t0 = MPI_Wtime();

    for (int it = 0; it < niters; ++it) {
        int idx = 0;
        // Post all nonblocking receives first
        for (int src = 0; src < nprocs; ++src) {
            if (src == rank) continue;
            MPI_Irecv(recvbufs[idx], msg_size, MPI_CHAR, src, 100, MPI_COMM_WORLD, &reqs_recv[idx]);
            idx++;
        }

        idx = 0;
        // Post all nonblocking sends
        for (int dst = 0; dst < nprocs; ++dst) {
            if (dst == rank) continue;
            MPI_Isend(buf, msg_size, MPI_CHAR, dst, 100, MPI_COMM_WORLD, &reqs_send[idx++]);
        }

        // Wait for all sends and receives
        MPI_Waitall(nprocs-1, reqs_send, MPI_STATUSES_IGNORE);
        MPI_Waitall(nprocs-1, reqs_recv, MPI_STATUSES_IGNORE);
    }

    double t1 = MPI_Wtime();

    double bytes_sent_oneway = (double)(nprocs - 1) * msg_size * niters;
    double rank_inj_bw_oneway = (bytes_sent_oneway / (t1 - t0)) / 1e9;

    printf("Rank %d: injection (one-way send only) = %.3f GB/s\n", rank, rank_inj_bw_oneway);

    // Node aggregate
    double node_inj = 0.0;
    MPI_Reduce(&rank_inj_bw_oneway, &node_inj, 1, MPI_DOUBLE, MPI_SUM, 0, nodecomm);
    if (local_rank == 0) printf("  [Node aggregate injection] = %.3f GB/s\n", node_inj);

    // Global aggregate
    MPI_Reduce(&rank_inj_bw_oneway, global_inj_oneway, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    *global_inj_bidirectional = *global_inj_oneway * 2.0;

    if (rank == 0) {
        printf("GLOBAL injection (one-way)       = %.3f GB/s\n", *global_inj_oneway);
        printf("GLOBAL injection (bidirectional) = %.3f GB/s (approx)\n\n", *global_inj_bidirectional);
    }

    // cleanup
    for (int i = 0; i < nprocs-1; i++) free(recvbufs[i]);
    free(recvbufs);
    free(reqs_send);
    free(reqs_recv);
}

// ------------------ Bisection Test ------------------
void run_bisection_test(int rank, int nprocs, char *buf,
                        int niters, size_t msg_size, double global_inj_oneway, double global_inj_bidirectional) {
    int half = nprocs / 2;
    if (half == 0) {
        if (rank == 0) fprintf(stderr, "Need at least 2 ranks for bisection test\n");
        return;
    }
    int partner = (rank < half) ? rank + half : rank - half;

    MPI_Barrier(MPI_COMM_WORLD);
    double tb0 = MPI_Wtime();

    for (int it = 0; it < niters; ++it) {
        MPI_Request r[2];
        MPI_Isend(buf, msg_size, MPI_CHAR, partner, 200, MPI_COMM_WORLD, &r[0]);
        MPI_Irecv(buf, msg_size, MPI_CHAR, partner, 200, MPI_COMM_WORLD, &r[1]);
        MPI_Waitall(2, r, MPI_STATUSES_IGNORE);
    }
    double tb1 = MPI_Wtime();

    double rank_bytes_two_way = (double)msg_size * niters * 2.0;
    double rank_bw_two_way = (rank_bytes_two_way / (tb1 - tb0)) / 1e9;

    printf("Rank %d: bisection (two-way with partner %d) = %.3f GB/s\n",
           rank, partner, rank_bw_two_way);

    double total_bis_bytes_two_way = 0.0;
    MPI_Reduce(&rank_bytes_two_way, &total_bis_bytes_two_way, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        double bis_total_bw_two_way = (total_bis_bytes_two_way / (tb1 - tb0)) / 1e9;
        double bis_total_bw_one_way = bis_total_bw_two_way / 2.0;
        printf("\nBisection measured (two-way aggregate) = %.3f GB/s\n", bis_total_bw_two_way);
        printf("Bisection measured (one-way aggregate) = %.3f GB/s\n", bis_total_bw_one_way);

        double nb_oneway = bis_total_bw_one_way / global_inj_oneway * 100.0;
        double nb_bidirectional = bis_total_bw_two_way / global_inj_bidirectional * 100.0;
        printf("Non-blocking %% (one-way)        = %.2f %%\n", nb_oneway);
        printf("Non-blocking %% (bidirectional) = %.2f %%\n\n", nb_bidirectional);
    }
}

// ------------------ Main ------------------
int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);

    int rank, nprocs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    // Node-local communicator
    MPI_Comm nodecomm;
    MPI_Comm_split_type(MPI_COMM_WORLD, MPI_COMM_TYPE_SHARED, 0, MPI_INFO_NULL, &nodecomm);
    int local_rank, local_size;
    MPI_Comm_rank(nodecomm, &local_rank);
    MPI_Comm_size(nodecomm, &local_size);

    size_t msg_size = DEFAULT_MSG_SIZE;
    int niters = DEFAULT_NITERS;

    // Mode selection
    int run_injection = 0, run_bisection = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--size") == 0 && i+1 < argc) msg_size = parse_size(argv[++i]);
        else if (strcmp(argv[i], "--iters") == 0 && i+1 < argc) niters = parse_iters(argv[++i]);
        else if (strcmp(argv[i], "--inject") == 0) run_injection = 1;
        else if (strcmp(argv[i], "--bisection") == 0) run_bisection = 1;
        else if (strcmp(argv[i], "--all") == 0) run_injection = run_bisection = 1;
    }

    if (!run_injection && !run_bisection) run_injection = run_bisection = 1; // default

    if (rank == 0) {
        printf("Running tests with msg_size=%zu bytes, niters=%d\n", msg_size, niters);
        printf("Modes: %s%s\n",
               run_injection ? "Injection " : "",
               run_bisection ? "Bisection" : "");
    }

    char *buf = malloc(msg_size);
    memset(buf, 1 + (rank % 7), msg_size);

    double global_inj_oneway = 0.0;
    double global_inj_bidirectional = 0.0;

    if (run_injection) {
        run_injection_test(rank, nprocs, local_rank, local_size, nodecomm, buf, niters,
                           msg_size, &global_inj_oneway, &global_inj_bidirectional);
    }

    if (run_bisection) {
        if (!run_injection) { // ensure injection values exist for %
            double inj_tmp = 0.0;
            MPI_Reduce(&inj_tmp, &global_inj_oneway, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
            global_inj_bidirectional = global_inj_oneway * 2.0;
        }
        run_bisection_test(rank, nprocs, buf, niters, msg_size, global_inj_oneway, global_inj_bidirectional);
    }

    free(buf);
    MPI_Comm_free(&nodecomm);
    MPI_Finalize();
    return 0;
}
