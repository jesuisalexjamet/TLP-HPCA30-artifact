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
    <!-- <li><a href="#about-the-framework">About the Framework</a></li>
    <li><a href="#prerequisites">Prerequisites</a></li>
    <li><a href="#installation">Installation</a></li>
    <li><a href="#preparing-traces">Preparing Traces</a></li>
    <li><a href="#experimental-workflow">Experimental Workflow</a></li>
      <ul>
        <li><a href="#launching-experiments">Launching Experiments</a></li>
        <li><a href="#rolling-up-statistics">Rolling up Statistics</a></li>
        <li><a href="#running-mcpat">Running McPAT</a></li>
      </ul>
    </li> -->
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

## License
Distributed under the MIT License. See `LICENSE` for more information.

## Contact
Alexandre Valentin Jamet - alexandre.jamet@bsc.es / alexandrev.jamet@gmail.com

<!-- ## Acknowledgments -->