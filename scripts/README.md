<p align="center">
  <h1 align="center">Script Documentation
  </h1>
</p>

<details open="open">
  <summary>Table of Contents</summary>
  <ol>
    <li><a href="#overview">Overview</a></li>
    <li><a href="#compiling-binaries">Compiling Binaries</a></li>
    <li><a href="#running-single-core-jobs">Running Single-Core Jobs</a></li>
  </ol>
</details>

## Overview

This file provides documentation of some of scripts present in this directory. Specifically, we discuss the `run_single_core.sh`, `run_single_core_legacy.sh`, and `compile_single_core.sh` scripts.

## Compiling Binaries

The `compile_single_core.sh` script automates the generation of all necessary experiment binaries within this artifact. By utilizing CMake, it handles the diverse configurations needed.

This script does not take any arguments and does not require its user to set any variable prior to its usage.

The usage of this script results in the creation of several directories under the `build/` directory. Each corresponding to CMake's output for one specific configuration. The different configurations are evetually built under the `bin/` directory. The scripts will result in the creation of six distinct version of the simulator:

 - `1_cores_cascade_lake_800mtps_legacy` and `1_cores_cascade_lake_800mtps` respectively corresponding to a baseline configuration using the ChampSim legacy trace format and an extended trace format for the GAP traces.
 - `1_cores_cascade_lake_hermes_o_800mtps_legacy` and `1_cores_cascade_lake_hermes_o_800mtps_legacy` respectively corresponding to a baseline configuration using the ChampSim legacy trace format and an extended trace format for the GAP traces.
 - `1_cores_cascade_lake_tlp_800mtps_legacy` and `1_cores_cascade_lake_tlp_800mtps_legacy` respectively corresponding to a baseline configuration using the ChampSim legacy trace format and an extended trace format for the GAP traces.

> *n.b.*: The extended trace format used for the GAP traces provides additional information about the memory regions that can potentially generate irregular access patterns. However, this information is not used for the context of this work.

## Running Single-Core Jobs

The `run_single_core.sh`, `run_single_core_legacy.sh`, and `run_single_core.job` scripts help the set of experiments required for this artifact. These scripts to not take any arguments. However, they contain variables that need to be set.

> *n.b.*: The simulation infrastructure assumes an environment in which SLURM is avilable to run jobs.

| Variable | Description | Script file(s) | Shoud be set by the user? |
|----------|-------------|----------------|---------------------------|
| `SLURM_USERNAME` | The username to provide SLURM commands with. | `run_single_core.sh`, `run_single_core_legacy.sh`, and `run_single_core.job`. | Yes |
| `GPFS_DIR` | The parent directory of the artifact. | `run_single_core.sh`, `run_single_core_legacy.sh`, and `run_single_core.job`. | Yes |
| `WORKING_DIR` | The full path to the artifact directory. | `run_single_core.sh`, `run_single_core_legacy.sh`, and `run_single_core.job`. | Yes |
| `TRACE_DIR` | The directory contaning the traces. | `run_single_core.sh`, `run_single_core_legacy.sh`, and `run_single_core.job`. | Yes |
| `OUTPUT_DIR` | The output directory for the simulation jobs. | `run_single_core.sh` and `run_single_core_legacy.sh`. | Possible. However, it is not recommended as this would imply extra work in other part of the artifact. |