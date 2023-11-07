#!/bin/bash
set -e

# Compiling the baseline (legacy & extended formats).
mkdir -p build/1_cores_cascade_lake_800mtps_legacy
mkdir -p build/1_cores_cascade_lake_800mtps

cd build/1_cores_cascade_lake_800mtps_legacy
cmake -G "Unix Makefiles" ../../ -DCMAKE_BUILD_TYPE=Release -DSIMULATOR_OUTPUT_DIRECTORY="1_cores_cascade_lake_800mtps_legacy" -DCHAMPSIM_CPU_NUMBER_CORE=1 -DCHAMPSIM_CPU_DRAM_IO_FREQUENCY=800 -DLEGACY_TRACE=ON -DENABLE_FSP=OFF -DENABLE_DELAYED_FSP=OFF -DENABLE_BIMODAL_FSP=OFF -DENABLE_SSP=OFF && make
cd ../1_cores_cascade_lake_800mtps
cmake -G "Unix Makefiles" ../../ -DCMAKE_BUILD_TYPE=Release -DSIMULATOR_OUTPUT_DIRECTORY="1_cores_cascade_lake_800mtps" -DCHAMPSIM_CPU_NUMBER_CORE=1 -DCHAMPSIM_CPU_DRAM_IO_FREQUENCY=800 -DLEGACY_TRACE=OFF -DENABLE_FSP=OFF -DENABLE_DELAYED_FSP=OFF -DENABLE_BIMODAL_FSP=OFF -DENABLE_SSP=OFF && make
cd ../../

# Compiling Hermes (legacy & extended formats).
mkdir -p build/1_cores_cascade_lake_hermes_o_800mtps_legacy
mkdir -p build/1_cores_cascade_lake_hermes_o_800mtps

cd build/1_cores_cascade_lake_hermes_o_800mtps_legacy
cmake -G "Unix Makefiles" ../../ -DCMAKE_BUILD_TYPE=Release -DSIMULATOR_OUTPUT_DIRECTORY="1_cores_cascade_lake_hermes_o_800mtps_legacy" -DCHAMPSIM_CPU_NUMBER_CORE=1 -DCHAMPSIM_CPU_DRAM_IO_FREQUENCY=800 -DLEGACY_TRACE=ON -DENABLE_FSP=ON -DENABLE_DELAYED_FSP=OFF -DENABLE_BIMODAL_FSP=OFF -DENABLE_SSP=OFF && make
cd ../1_cores_cascade_lake_hermes_o_800mtps
cmake -G "Unix Makefiles" ../../ -DCMAKE_BUILD_TYPE=Release -DSIMULATOR_OUTPUT_DIRECTORY="1_cores_cascade_lake_hermes_o_800mtps" -DCHAMPSIM_CPU_NUMBER_CORE=1 -DCHAMPSIM_CPU_DRAM_IO_FREQUENCY=800 -DLEGACY_TRACE=OFF -DENABLE_FSP=ON -DENABLE_DELAYED_FSP=OFF -DENABLE_BIMODAL_FSP=OFF -DENABLE_SSP=OFF && make
cd ../../

# Compiling TLP (legacy & extended formats).
mkdir -p build/1_cores_cascade_lake_tlp_800mtps_legacy
mkdir -p build/1_cores_cascade_lake_tlp_800mtps

cd build/1_cores_cascade_lake_tlp_800mtps_legacy
cmake -G "Unix Makefiles" ../../ -DCMAKE_BUILD_TYPE=Release -DSIMULATOR_OUTPUT_DIRECTORY="1_cores_cascade_lake_tlp_800mtps_legacy" -DCHAMPSIM_CPU_NUMBER_CORE=1 -DCHAMPSIM_CPU_DRAM_IO_FREQUENCY=800 -DLEGACY_TRACE=ON -DENABLE_FSP=ON -DENABLE_DELAYED_FSP=OFF -DENABLE_BIMODAL_FSP=ON -DENABLE_SSP=ON && make
cd ../1_cores_cascade_lake_tlp_800mtps
cmake -G "Unix Makefiles" ../../ -DCMAKE_BUILD_TYPE=Release -DSIMULATOR_OUTPUT_DIRECTORY="1_cores_cascade_lake_tlp_800mtps" -DCHAMPSIM_CPU_NUMBER_CORE=1 -DCHAMPSIM_CPU_DRAM_IO_FREQUENCY=800 -DLEGACY_TRACE=OFF -DENABLE_FSP=ON -DENABLE_DELAYED_FSP=OFF -DENABLE_BIMODAL_FSP=ON -DENABLE_SSP=ON && make
cd ../../

# Copying prefetchers and replacement policies plugins.
cp bin/prefetchers/* prefetchers/
cp bin/replacements/* replacements/

cp prefetchers/libl1d_ipcp.so prefetchers/libl1d_ipcp_iso.so
