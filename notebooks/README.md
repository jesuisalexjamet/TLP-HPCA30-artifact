<p align="center">
  <h1 align="center">Jupyter Notebooks Documentation
  </h1>
</p>

<details open="open">
  <summary>Table of Contents</summary>
  <ol>
    <li><a href="#overview">Overview</a></li>
    <li><a href="#installation">Installation</a></li>
    <li><a href="#compiling-binaries">Running the Single-Core Analysis Notebook</a></li>
  </ol>
</details>

## Overview

This README contains documentation of the analysis Jupyter Notebook(s) provided in this artifact. Specifically, we discuss the installation of the required packages and software along with the `single_core.ipynb` Jupyter Notebook.

## Installation

While developing and experimenting on this work we relied on Jupyter Notebook as the backbone of out analysis workflow. Specifically, we leveraged Jupyter Notebook in the Visual Studio Code environment.

To reproduce this working environment, one needs to follow these steps:

 1. Install the VS Code IDE. The IDE can be downloaded on [Microsoft's web site](https://code.visualstudio.com/).
 2. Once the IDE is installed, one needs to download an install all packages required to execute Jupyter Notebooks in this IDE. Using the following command, one will be able to install all these packages

 ```shell
code --install-extension ms-toolsai.jupyter
code --install-extension ms-toolsai.jupyter-keymap
code --install-extension ms-toolsai.jupyter-renderers
code --install-extension ms-toolsai.vscode-jupyter-cell-tags
code --install-extension ms-toolsai.vscode-jupyter-slideshow
 ```

An alternative to using the aforementioned commands can be to install the packages through the Visual Strudio Code's package explorer. The packages names are the following:

 - Jupyter
 - Jupyter-keymap
 - Jupyter-renderers

## Running the Single-Core Analysis Notebook

Once the installation of the different softwares and packages is completed, one only needs to open the `single_core.ipynb` notebook with the Visual Studio Code IDE from the `TLP-HPCA30-artifact` workspace.