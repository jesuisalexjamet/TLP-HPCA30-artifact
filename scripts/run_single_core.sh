#!/bin/bash
set -e

SLURM_USERNAME="ajamet" # Provide a SLURM username.

# Setting a couple of variable to set the environment.
GPFS_DIR="/scratch/nas/3/${SLURM_USERNAME}" # Where is the AE directory stored?
WORKING_DIR="${GPFS_DIR}/TLP-HPCA30-artifact"

TRACE_DIR="${GPFS_DIR}/traces/"
OUTPUT_DIR="${WORKING_DIR}/results/single_core/100M/100M" # Provide an output directory.

# Defining a useful functions that will allow us to know how many jobs are already in the queues.
jobs_in_queue() {
  lines=$(squeue -u ${SLURM_USERNAME} | wc -l)
  lines=$((lines - 1))

  echo $lines
}

# Defining a second, yet useful function, that allows you to wait or carry on depending on two parameters.
# The first one is the current total number of jobs in the queues of the cluster.
# The second is the maximum number of jobs allowed in the queue at ones (e.g.: in SERT this value is 10k).
should_wait() {
  current_num_jobs=$1
  limit=$2

  if (("$current_num_jobs" >= "$limit")); then
    sleep 30m
    echo "1"
  else
    echo "0"
  fi
}

# Getting the list of traces.
TRACES_ARRAY=()
idx=0

echo $TRACE_DIR

for trace in ${TRACE_DIR}/*.*.sdc-*.xz; do
    filename=$(basename -- $trace)


	TRACES_ARRAY[$idx]=$filename
	idx=$((idx + 1))
done
unset idx

echo ${TRACES_ARRAY[@]}

# ChampSim configurations.
CONFIGS=(	
	"baseline_cascade_lake_no_prefetchers"

	"baseline_cascade_lake_ipcp"
	"baseline_cascade_lake_ipcp_spp_ppf"
	"baseline_cascade_lake_ipcp_hermes_o" 
	"baseline_cascade_lake_ipcp_spp_ppf_hermes_o"
	"baseline_cascade_lake_ipcp_hermes_o_double"   
	"baseline_cascade_lake_ipcp_tlp_layered_core_l1d_f20_-25"
	"baseline_cascade_lake_ipcp_iso_prefetcher"
	
	"baseline_cascade_lake_berti"
	"baseline_cascade_lake_berti_spp_ppf"
	"baseline_cascade_lake_berti_hermes_o"
	"baseline_cascade_lake_berti_spp_ppf_hermes_o"
	"baseline_cascade_lake_berti_hermes_o_double" 
	"baseline_cascade_lake_berti_tlp_layered_core_l1d_f20_-25"
	"baseline_cascade_lake_berti_iso_prefetcher"
)

BINARIES=(
    "1_cores_cascade_lake_800mtps"

    "1_cores_cascade_lake_800mtps"
    "1_cores_cascade_lake_800mtps"
    "1_cores_cascade_lake_hermes_o_800mtps"
    "1_cores_cascade_lake_hermes_o_800mtps"
    "1_cores_cascade_lake_hermes_o_800mtps"
    "1_cores_cascade_lake_tlp_800mtps"
    "1_cores_cascade_lake_800mtps"

    "1_cores_cascade_lake_800mtps"
    "1_cores_cascade_lake_800mtps"
    "1_cores_cascade_lake_hermes_o_800mtps"
    "1_cores_cascade_lake_hermes_o_800mtps"
    "1_cores_cascade_lake_hermes_o_800mtps"
    "1_cores_cascade_lake_tlp_800mtps"
    "1_cores_cascade_lake_800mtps"
)

echo ${CONFIGS[@]}

idx=0

for config in ${CONFIGS[@]}; do
    for trace in ${TRACES_ARRAY[@]}; do
        JOB_NAME="${config}-${trace}"
        OUTPUT_FILE="${OUTPUT_DIR}/${config}/${trace}.txt"
        ERROR_FILE="${OUTPUT_DIR}/${config}/${trace}.err"

	should_wait_ret_code="1"
        sbatch_ret_code="1"

        # Waiting for the queue to empty a bit.
        while (($should_wait_ret_code != 0)); do
            count=$(jobs_in_queue)
            should_wait_ret_code=$(should_wait $count 10000)
        done

    	# Creating the output directory if doesn't exist yet.
    	mkdir -p ${OUTPUT_DIR}/${config}

        # Launching the job ensuring that it has been taken into account.
        while (($sbatch_ret_code != 0)); do
            sbatch --job-name=${JOB_NAME} --output=${OUTPUT_FILE} --error=${ERROR_FILE} --chdir=${WORKING_DIR} --requeue --mail-type=FAIL,TIME_LIMIT --mail-user=alexandre.jamet@bsc.es ${WORKING_DIR}/scripts/run_single_core.job config/${config}.json ${BINARIES[$idx]} ${trace}
            sbatch_ret_code=$?
        done
    done

    # Incrementing the index counter.
    idx=$((idx + 1))
done
unset idx
