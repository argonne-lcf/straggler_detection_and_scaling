#!/bin/bash -x

# You need to 
# 1. copy this job script to your lustre project working directory 
# 2. update #PBS -A allocation_name
# 3. update DAOS_POOL name
# and then run this job script with qsub command.

#PBS -l select=192
#PBS -l walltime=00:15:00
#PBS -A datascience
#PBS -q prod
#PBS -k doe
#PBS -ldaos=daos_user
#PBS -l filesystems=home:flare:daos_user
#PBS -N daos-peak-test

# qsub daos_peak_test_job.sh
# or qsub -l select=192:ncpus=208 -l walltime=01:00:00 -A datascience -l daos=daos_user -l filesystems=home:flare:daos_user -q prod -I


#readme for IOR
# -F	file-per-process        - No -F is single shared file [This is an I/O Access parameter] 
# -c	collective I/O            [I/O type parameter] with -a mpiio and add daos:/tmp/$DAOS_POOL/$DAOS_CONT/io.dat in -o
# No -c independent I/O           [I/O type parameter] with -a posix and just /tmp/$DAOS_POOL/$DAOS_CONT/io.dat in -o
# -C	reorderTasksConstant    – changes task ordering to n+1 ordering for readback
# -e	fsync                   – perform fsync upon POSIX write close
# -D N	deadlineForStonewalling – seconds before stopping write or read phase
# -b N	blockSize               – contiguous bytes to write per task (e.g.: 8, 4k, 2m, 1g)
# -t N	transferSize            – size of transfer in bytes (e.g.: 8, 4k, 2m, 1g)
# -i N	repetitions             – number of repetitions of test

date
DAOS_POOL=datascience
DAOS_CONT=peak_bw_test_cont

cd $PBS_O_WORKDIR
module use /soft/modulefiles
module load daos
echo Jobid: $PBS_JOBID
echo Running on nodes `cat $PBS_NODEFILE`
NNODES=`wc -l < $PBS_NODEFILE`
RANKS_PER_NODE=64
NRANKS=$(( NNODES * RANKS_PER_NODE ))
CPU_BINDING=list:4:56:5:57:6:58:7:59:8:60:9:61:10:62:11:63:12:64:13:65:14:66:15:67:16:68:17:69:18:70:19:71:20:72:21:73:22:74:23:75:24:76:25:77:26:78:27:79:28:80:29:81:30:82:31:83:32:84:33:85:34:86:35:87:36:88:37:89:38:90:39:91:40:92:41:93:42:94:43:95:44:96:45:97:46:98:47:99:48:100:49:101:50:102:51:103 
echo "NUM_OF_NODES=${NNODES}  TOTAL_NUM_RANKS=${NRANKS}  RANKS_PER_NODE=${RANKS_PER_NODE} CPU_BINDING=${CPU_BINDING}"
IOR_INSTALL=/soft/daos/examples/daos_peak_test/ior_mdtest_install_bin/
export LD_LIBRARY_PATH=$IOR_INSTALL/lib:$LD_LIBRARY_PATH
export PATH=$IOR_INSTALL/bin:$PATH

which daos
daos version 
pidof daos_agent
ps -ef|grep daos

# daos pool autotest $DAOS_POOL         # Takes additional 10 minutes to complete. Run only when there is a problem.
daos pool query ${DAOS_POOL}
daos container destroy   ${DAOS_POOL} ${DAOS_CONT}
daos container create --type POSIX ${DAOS_POOL}  ${DAOS_CONT} --properties=cksum:crc32,srv_cksum:on,rd_fac:2,ec_cell_sz:131072 --file-oclass=EC_16P2GX --dir-oclass=RP_3G1  --chunk-size=2097152
daos container check     ${DAOS_POOL} ${DAOS_CONT}
daos container query     ${DAOS_POOL} ${DAOS_CONT}
daos container get-prop  ${DAOS_POOL} ${DAOS_CONT}
time launch-dfuse.sh     ${DAOS_POOL}:${DAOS_CONT}
mount|grep dfuse   
mpiexec --env LD_PRELOAD=/usr/lib64/libpil4dfs.so -np ${NRANKS} -ppn ${RANKS_PER_NODE} --cpu-bind ${CPU_BINDING} --no-vni -genvall ior -a posix -i 5 -b 1g -t 2m -D 100 -w -r -C -e -v -o /tmp/${DAOS_POOL}/${DAOS_CONT}/io_file.dat
# mpiexec                                           -np ${NRANKS} -ppn ${RANKS_PER_NODE} --cpu-bind ${CPU_BINDING} --no-vni -genvall ior -a DFS --dfs.pool=${DAOS_POOL} --dfs.cont=${DAOS_CONT} --dfs.oclass=EC_16P2GX --dfs.dir_oclass=RP_3G1 -i 5 -b 1g -t 2m -D 100 -w -r -C -e -v -o /io2.dat 
clean-dfuse.sh ${DAOS_POOL}:${DAOS_CONT}
daos container destroy   ${DAOS_POOL}  ${DAOS_CONT}

daos container create --type POSIX ${DAOS_POOL}  ${DAOS_CONT} --properties=cksum:crc32,srv_cksum:on,rd_fac:0,ec_cell_sz:131072 --file-oclass=SX --dir-oclass=S1 --chunk-size=2097152
daos container check     ${DAOS_POOL} ${DAOS_CONT}
daos container query     ${DAOS_POOL} ${DAOS_CONT}
daos container get-prop  ${DAOS_POOL} ${DAOS_CONT}
time launch-dfuse.sh     ${DAOS_POOL}:${DAOS_CONT}
mount|grep dfuse  
mpiexec -np ${NRANKS} -ppn ${RANKS_PER_NODE} --cpu-bind ${CPU_BINDING} --no-vni -genvall ior -a DFS --dfs.pool=${DAOS_POOL} --dfs.cont=${DAOS_CONT} --dfs.oclass=SX --dfs.dir_oclass=S1 --dfs.chunk_size=2097152 -i 5 -b 1g -t 2m -D 100 -w -r -C -e -v -o /io2.dat 
clean-dfuse.sh ${DAOS_POOL}:${DAOS_CONT}
daos container destroy   ${DAOS_POOL}  ${DAOS_CONT}
date


# mpiexec -np ${NRANKS} -ppn ${RANKS_PER_NODE} --cpu-bind ${CPU_BINDING} --no-vni -genvall ior -a MPIIO -o daos://40a43e2c-d6cb-4a80-8b2e-5bc5cb22685b/d7f35947-f30d-443b-b56e-9f06211956d1/file.out -F -w -b 10g -t 4m -i 3