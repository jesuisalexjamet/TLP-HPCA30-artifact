<p align="center">
  <h3 align="center">A Two Level Neural Approach Combining Off-Chip Prediction with Adaptive Prefetch Filtering</h3>
</p>

<p align="center">
<img alt="GitHub last commit" src="https://img.shields.io/github/last-commit/itisntalex/TLP-HPCA30-artifact" />
<a href="https://github.com/itisntalex/TLP-HPCA30-artifact/releases">
    <img alt="GitHub release (latest by date)" src="https://img.shields.io/github/v/release/itisntalex/TLP-HPCA30-artifact"></a>
    <a href="https://github.com/itisntalex/TLP-HPCA30-artifact/blob/main/LICENSE">
        <img alt="GitHub" src="https://img.shields.io/badge/License-MIT-yellow.svg" />
    </a>
    <a href="https://twitter.com/intent/follow?screen_name=jesuisalexjamet">
    <img src="https://img.shields.io/twitter/follow/jesuisalexjamet" alt="Twitter Follow" /></a>
</p>

<details open="open">
  <summary>Table of Contents</summary>
  <ol>
    <li><a href="#what-is-tlp">What is TLP?</a></li>
    <li><a href="#about-the-framework">About the Framework</a></li>
    <li><a href="#prerequisites">Prerequisites</a></li>
    <li><a href="#installation">Installation</a></li>
    <li><a href="#preparing-traces">Preparing Traces</a></li>
    <li><a href="#experimental-workflow">Experimental Workflow</a></li>
      <ul>
        <li><a href="#launching-experiments">Launching Experiments</a></li>
        <li><a href="#rolling-up-statistics">Rolling up Statistics</a></li>
      </ul>
    </li>
    <!-- <li><a href="#brief-code-walkthrough">Brief Code Walkthrough</a></li>
    <li><a href="#frequently-asked-questions">Frequently Asked Questions</a></li>
    <li><a href="#citation">Citation</a></li> -->
    <li><a href="#license">License</a></li>
    <li><a href="#contact">Contact</a></li>
    <!-- <li><a href="#acknowledgments">Acknowledgments</a></li> -->
  </ol>
</details>

## What is TLP?

> The *Two Level Perceptron* (TLP) predictor is a neural mechanism that
effectively combines predicting whether an access will be off-chip
with adaptive prefetch filtering at the first-level data cache (L1D).

The key idea behind TLP is to: 
 1. Accurately predict which load requests might go to off-chip;
 2. Accurately predict which L1D prefetch request might go off-chip;
 3. Speculatively start fetching the data required by the predicted off-chip loads directly from the main memory, when confidence is high enough, in parallel to the cache accesses. Conversely, when confidence is not high enough, the speculative fetch from main memory is delayed upon an L1D miss.
 4. Discard L1D prefetch request that are predicted to go off-chip.

TLP has been accepted at the [2024 IEEE International Symposium on High-Performance Computer Architecture](https://hpca-conf.org/2024/).

## About the Framework

TLP is modleed in the [ChampSim Simulator](https://github.com/ChampSim/ChampSim). It is fully compatible with all publicly-available traces for ChampSim.

## Prerequisites

The infrastructure has been tested with the following system configuration:

1. CMake 3.16.3
2. GCC 9.4.0
3. Boost 1.71.0
4. VSCode 1.84
5. VSCode Jupyter Extension v2023.10
6. Python 3.9.10

## Instalation

1. To clone the GitHub repository use on the following command in your favorite bash interpreter.

  ```bash
  git clone git@github.com:itisntalex/TLP-HPCA30-artifact.git
  git clone https://github.com/itisntalex/TLP-HPCA30-artifact.git
  ```

2. Build the simulation infrastructure using CMake. A collection of variable can be provided to customize the build.

```bash
cd build
cmake -G "Unix Makefiles" ../ -DSIMULATOR_OUTPUT_DIRECTORY="1_core_cascade_lake" -DCHAMPSIM_CPU_NUMBER_CORE=1 -DCHAMPSIM_CPU_DRAM_IO_FREQUENCY=800 -DLEGACY_TRACE=ON -DENABLE_FSP=OFF -DENABLE_DELAYED_FSP=OFF -DENABLE_BIMODAL_FSP=OFF -DENABLE_SSP=OFF
make -j
```

The CMake buildsystem for our infrasture accepts a handful of parameters that allow to customize the build:
- `SIMULATOR_OUTPUT_DIRECTORY`: Specifies the directory in which the simulator will be compiled `bin/${SIMULATOR_OUTPUT_DIRECTORY}`.
- `CHAMPSIM_CPU_NUMBER_CORE`: Specifies the number of cores that will be simulated.
- `CHAMPSIM_CPU_DRAM_IO_FREQUENCY`: Specifies the I/O frequency of modeled DRAM.
- `LEGACY_TRACE`: Specifies whether or not to use the legacy ChampSim trace format.
- `ENABLE_FSP`: Specifies wheter or not to use the FSP predictor (equivalent to Hermes).
- `ENABLE_DELAYED_FSP`: Specifies whether or not to use the Delayed FSP predictor.
- `ENABLE_BIMODAL_FSP`: Specifies whether or not to use the Bimoal FSP predictor.
- `ENABLE_SSP`: Specifies whether or not to use the SSP predictor.

## Preparing Traces

We provide the traces used for simulation in three volumes. The traces, in total, represent around 145GB of data. The following three Zenodo records allow to download all traces:
1. Volume 1: [![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.10083542.svg)](https://doi.org/10.5281/zenodo.10083542)
2. Volume 2: [![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.10088347.svg)](https://doi.org/10.5281/zenodo.10088347)
3. Volume 3: [![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.10088525.svg)](https://doi.org/10.5281/zenodo.10088525)

Here are the steps to prepare the traces for the workflow.

1. Download the traces from the Zenodo records above.
2. Place the three volumes of the trace archive at the root of the artifact folder.
3. Extract the traces using the following command in your favorite shell:
```shell
tar -xMf TLP-HPCA30-artifact-traces.VOLUME1.tar
> n TLP-HPCA30-artifact-traces.VOLUME2.tar
> n TLP-HPCA30-artifact-traces.VOLUME3.tar
```

When the extraction of the first volume is completed, the shell will request the name of the second volume. Provide it as follows `> n TLP-HPCA30-artifact-traces.VOLUME1.tar`. Repeat the operation until the three volumes are consumed.

A new directory named `traces` should be available, containing all the traces.

## Experimental Workflow

Our experimental workflow consist of two stages: i) running the experiments, and ii) running python scripts through Jupyter notebooks.

### Launching Experiments

1. To run the experiments, start by setting the values of the different variables in the `scripts/run_single_core.sh`, `scripts/run_single_core_legacy.sh`, and `scripts/run_single_core.job`.

2. Run the following commands to run the full set of experiment required for this workflow.

```shell
./scripts/run_single_core.sh
./scripts/run_single_core_legacy.sh
```

> *N.B.*: The scripts running the experiments assume that one has access to a computing cluster using slurm.

### Rolling up Statistics

In this workflow, rolling-up statistics is very simple. In `notebooks` we provide a Jupyter notebook called `scripts/single_core.ipynb` that contains all the code necessary to run the analysis of the results and provide different figures.

To use this notebook, one only needs to open it using Visual Studio Code and run it. In our original methodology, we use the Jupyter extension of Visual Studio Code to run the notebooks in the IDE. Installing the required Visual Studio Code extension can be done through the command line using the following commands:

```shell
code --install-extension ms-toolsai.jupyter
code --install-extension ms-toolsai.jupyter-keymap
code --install-extension ms-toolsai.jupyter-renderers
code --install-extension ms-toolsai.vscode-jupyter-cell-tags
code --install-extension ms-toolsai.vscode-jupyter-slideshow
```

## License
Distributed under the MIT License. See `LICENSE` for more information.

## Contact
Alexandre Valentin Jamet - alexandre.jamet@bsc.es / alexandrev.jamet@gmail.com

<!-- ## Acknowledgments -->