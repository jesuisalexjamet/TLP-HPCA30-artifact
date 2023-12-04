#!/bin/bash
set -e

# Installing the boost library (dev) package.
sudo apt install libboost-all-dev

# Installing IPython.
pip3 install ipython

# Installing the VS Code IDE through the command line (this assumes a Ubuntu system).
sudo snap install --classic code

# Installing VS Code extensions required to run the artifact and specifically to create the plots.
code --install-extension ms-toolsai.jupyter
code --install-extension ms-toolsai.jupyter-keymap
code --install-extension ms-toolsai.jupyter-renderers
code --install-extension ms-toolsai.vscode-jupyter-cell-tags
code --install-extension ms-toolsai.vscode-jupyter-slideshow