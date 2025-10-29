<p align="center">
  <h3 align="center">A Two Level Neural Approach Combining Off-Chip Prediction with Adaptive Prefetch Filtering</h3>
</p>

<p align="center">
<img alt="GitHub last commit" src="https://img.shields.io/github/last-commit/jesuisalexjamet/TLP-HPCA30-artifact" />
  <a href="https://github.com/jesuisalexjamet/TLP-HPCA30-artifact/blob/main/LICENSE">
      <img alt="GitHub" src="https://img.shields.io/badge/License-MIT-yellow.svg" />
  </a>
  <a href="https://doi.org/10.5281/zenodo.10100304"><img src="https://zenodo.org/badge/DOI/10.5281/zenodo.10100304.svg" alt="DOI"></a>
  <a href="https://register.epo.org/application?number=EP23383348">
      <img alt="Patent" src="https://img.shields.io/badge/Patent-EP4575807A1-blue.svg" />
  </a>
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
        <li><a href="#installing-slurm-for-bare-metal-clusters">Installing SLURM for bare-metal Clusters</a></li>
        <li><a href="#launching-experiments">Launching Experiments</a></li>
        <li><a href="#rolling-up-statistics">Rolling up Statistics</a></li>
      </ul>
    </li>
    <!-- <li><a href="#brief-code-walkthrough">Brief Code Walkthrough</a></li> -->
    <li><a href="#frequently-asked-questions">Frequently Asked Questions</a></li>
    <li><a href="#citation">Citation</a></li>
    <li><a href="#patent">Patent</a></li>
    <li><a href="#presentations">Presentations</a></li>
    <li><a href="#license">License</a></li>
    <li><a href="#contact">Contact</a></li>
    <li><a href="#acknowledgments">Acknowledgments</a></li>
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
7. IPython
8. `libboost-all-dev` on Ubuntu systems
9. Various Tex related packages.

## Instalation

1. To clone the GitHub repository use on the following command in your favorite bash interpreter.

  ```bash
  git clone git@github.com:jesuisalexjamet/TLP-HPCA30-artifact.git
  git clone https://github.com/jesuisalexjamet/TLP-HPCA30-artifact.git
  ```

Alternatively, one can use the `scripts/install_dependencies.sh` as follows:

```bash
./scripts/install_dependencies.sh
```

2. Intall necessary prerequisites (These commands install both the Boost libraries, the Visual Studio Code IDE, and IPython).

```bash
sudo apt install libboost-all-dev
sudo snap install --classic code
sudo pip3 install ipython
sudo apt-get install dvipng texlive-latex-extra texlive-fonts-recommended cm-super
```

3. Build the simulation infrastructure using CMake. A collection of variable can be provided to customize the build.

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
- `ENABLE_FSP`: Specifies wheter or not to use the FSP predictor (equivalent to TLP-HPCA30-artifact).
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

When the extraction of the first volume is completed, the shell will request the name of the second volume. Provide it as follows `> n TLP-HPCA30-artifact-traces.VOLUME2.tar`. Repeat the operation until the three volumes are consumed.

A new directory named `traces` should be available, containing all the traces.

## Experimental Workflow

Our experimental workflow consist of two stages: i) running the experiments, and ii) running python scripts through Jupyter notebooks.

### Installing SLURM for bare-metal Clusters

If one needs or wishes to use a bare-metal cluster to run the simulations required for this artifact, instructions for building and installing SLURM can be found [here](https://slurm.schedmd.com/quickstart_admin.html#build_install).

> *N.B.*: Please note that this artifact **requires** SLURM to be available as the computational power required to run all these experiments is very high (*i.e.*, running on an academic cluster, the experiments required to obtain the single-core results would take up to 12 hours to complete).

### Launching Experiments

1. To run the experiments, start by setting the values of the different variables (as mentionned here [here](https://github.com/jesuisalexjamet/TLP-HPCA30-artifact/tree/main/scripts#running-single-core-jobs)) in the `scripts/run_single_core.sh`, `scripts/run_single_core_legacy.sh`, and `scripts/run_single_core.job`.

2. Run the following commands to run the full set of experiment required for this workflow.

```shell
./scripts/run_single_core.sh
./scripts/run_single_core_legacy.sh
```

Upon the execution of these simulations, the result files will be placed in the `results` directory.

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

Alternatively, could use the plain IPython script provided in the `notebooks` directory. To do so, simply run the following command in your terminal from the root directory of the artifact:

```shell
ipython ./notebooks/single_core.py
```

## Frequently Asked Questions

1. **The workflow does not work when I execute the [scripts/run_single_core.sh](scripts/run_single_core.sh)/[scripts/run_single_core_legacy.sh](scripts/run_single_core_legacy.sh). Why is that?**

There is a chance you might have forgotten to set the different variables. Please refer to (this document)[scripts/README.md] to

2. **How much time out should I allocate for each job?**

We recommend allocating 12 hours to each jobs. This will allow you to safely run each job without risking a job to timeout before its completion.


## Citation

If you want to cite this work, please cite the following paper:
```bibtex
@INPROCEEDINGS{10476485,
author={Jamet, Alexandre Valentin and Vavouliotis, Georgios and Jiménez, Daniel A. and Alvarez, Lluc and Casas, Marc},
booktitle={2024 IEEE International Symposium on High-Performance Computer Architecture (HPCA)}, 
title={A Two Level Neural Approach Combining Off-Chip Prediction with Adaptive Prefetch Filtering}, 
year={2024},
volume={},
number={},
pages={528-542},
abstract={To alleviate the performance and energy overheads of contemporary applications with large data footprints, we propose the Two Level Perceptron (TLP) predictor, a neural mechanism that effectively combines predicting whether an access will be off-chip with adaptive prefetch filtering at the first-level data cache (L1D). TLP is composed of two connected microarchitectural perceptron predictors, named First Level Predictor (FLP) and Second Level Predictor (SLP). FLP performs accurate off-chip prediction by using several program features based on virtual addresses and a novel selective delay component. The novelty of SLP relies on leveraging off-chip prediction to drive L1D prefetch filtering by using physical addresses and the FLP prediction as features. TLP constitutes the first hardware proposal targeting both off-chip prediction and prefetch filtering using a multilevel perceptron hardware approach. TLP only requires 7KB of storage. To demonstrate the benefits of TLP we compare its performance with state-of-the-art approaches using off-chip prediction and prefetch filtering on a wide range of single-core and multi-core workloads. Our experiments show that TLP reduces the average DRAM transactions by 30.7% and 17.7%, as compared to a baseline using state-of-the-art cache prefetchers but no off-chip prediction mechanism, across the single-core and multi-core workloads, respectively, while recent work significantly increases DRAM transactions. As a result, TLP achieves geometric mean performance speedups of 6.2% and 11.8% across single-core and multi-core workloads, respectively. In addition, our evaluation demonstrates that TLP is effective independently of the L1D prefetching logic.},
keywords={Pollution;Microarchitecture;Filtering;Prefetching;Memory management;Random access memory;Bandwidth;Hardware Prefetching;Off-Chip Prediction;Prefetch Filtering;micro-architecture;Graph-Processing},
doi={10.1109/HPCA57654.2024.00046},
ISSN={2378-203X},
month={March},
}
```

If you use this repository, please cite it using the following:
```bibtex
@software{Jamet_TLP-HPCA30-Artifact,
author = {Jamet, Alexandre Valentin and Vavouliotis, Georgios and Jiménez, Daniel A. and Alvarez, Lluc and Casas, Marc},
license = {MIT},
title = {{TLP-HPCA30-Artifact}},
url = {https://github.com/jesuisalexjamet/TLP-HPCA30-artifact}
}
```

## Patent

This work is protected under the following patent:

> **Perceptron-Based Off-Chip Predictor**  
> *European Patent Application*: EP 4 575 807 A1  
> *Application number*: 23383348.2  
> *Filing date*: 21 December 2023  
> *Publication date*: 25 June 2025 (Bulletin 2025/26)  
> *Applicant*: Barcelona Supercomputing Center – Centro Nacional de Supercomputación (BSC)  
> *Inventors*: Alexandre Valentin Jamet, Georgios Vavouliotis, Marc Casas  
>
> The patent covers the first-level (FLP) and two-level (TLP) perceptron-based off-chip predictors described in this repository, which selectively delay off-chip predictions using dual thresholds to improve DRAM transaction efficiency.

For more details, see the full patent at the [European Patent Office (EP 4575807 A1)](https://register.epo.org/application?number=EP23383348).

## Presentations

This work has been presented at the following venues:

- **[2024 IEEE International Symposium on High-Performance Computer Architecture (HPCA 30)](https://hpca-conf.org/)**  
  *Edinburgh, United Kingdom — March 2024*  
  DOI: [10.1109/HPCA57654.2024.00046](https://doi.org/10.1109/HPCA57654.2024.00046)

- **[APPT 2025 Young Scholar Forum (YSF)](https://www.appt-conference.com/)**  
  *Athens, Greece — September 2025*  
  The presentation highlighted the practical implications of perceptron-based off-chip prediction mechanisms and their integration into adaptive prefetch filtering systems.

## License
Distributed under the MIT License. See `LICENSE` for more information.

## Contact
 - [Alexandre Valentin Jamet](https://dblp.org/pid/279/2596.html) (Universitat Politecnica de Catalunya / Barcelona Supercomputing Center) - alexandre.jamet@bsc.es / alexandrev.jamet@gmail.com
 - [Georgios Vavouliotis](https://dblp.org/pid/298/8514.html) (Huawei Zurich Research Center) - georgios.vavouliotis2@huawei.com
 - [Daniel A. Jiménez](https://dblp.org/pid/96/2151.html) (Texas A&M University) - djimenez@acm.org
 - [Lluc Alvarez](https://dblp.org/pid/06/2988.html) (Universitat Politecnica de Catalunya / Barcelona Supercomputing Center) - lluc.alvarez@bsc.es
 - [Marc Casas](https://dblp.org/pid/68/6352.html) (Universitat Politecnica de Catalunya / Barcelona Supercomputing Center) - marc.casas@bsc.es

## Acknowledgments

The authors are grateful to the anonymous MICRO 2023 reviewers for their valuable comments and constructive feedback that significantly improved the quality of the paper. This work is supported by the National Science Foundation through grant CCF-1912617 and generous gifts from Intel. Marc Casas has been partially supported by the Grant RYC-2017-23269 funded by MCIN/AEI/10.13039/501100011033 and by ESF Investing in your future. This research was supported by grant PID2019-107255GB-C21 funded by MCIN/AEI/ 10.13039/501100011033. Els autors agraeixen el suport del Departament de Recerca i Universitats de la Generalitat de Catalunya al Grup de Recerca "Performance understanding, analysis, and simulation/emulation of novel architectures" (Codi: 2021 SGR 00865).