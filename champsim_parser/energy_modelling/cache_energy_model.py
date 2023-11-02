#!/usr/bin/env python3

import numpy as np


class CacheEnergyModel:
    def __init__(self, name, load_energy, store_energy, leakage_power):
        self._name = name

        # Initializing the cache energy model with null values.
        self._load_energy = load_energy * (10 ** -9)
        self._store_energy = store_energy * (10 ** -9)

        self._leakage_power = leakage_power * (10 ** -3)

    @property
    def name(self):
        return self._name

    @name.setter
    def name(self, value):
        self._name = value

    @property
    def load_energy(self):
        return self._load_energy

    @load_energy.setter
    def load_energy(self, value):
        self._load_energy = value

    @property
    def store_energy(self):
        return self._store_energy

    @store_energy.setter
    def store_energy(self, value):
        self._store_energy = value

    @property
    def leakage_power(self):
        return self._leakage_power

    @leakage_power.setter
    def leakage_power(self, value):
        self._leakage_power = value

    def compute(self, loads=0, stores=0, time=0.0):
        output = {
            'dynamic_energy': 0.0,
            'static_energy': 0.0,
        }
        # First, we build a vector made of the parameters passed to the method.
        params = np.array([loads, stores, time])
        model = np.array([self._load_energy, self._load_energy, self._leakage_power])

        total_energy = params * model

        output['dynamic_energy'] = np.sum(total_energy[1:])
        output['static_energy'] = total_energy[0]

        # Returning a dictionary containing both the static and the dynamic energy consumed by the cache.
        #
        # TODO: This method must return a proper instance of a new class describing the results of the energy model.
        return output

    def __str__(self):
        return f'Cache energy model for {self._name}\n' \
               f'\t- Load energy {self._load_energy:.3} J/access\n' \
               f'\t- Sotre energy {self._store_energy:.3} J/access\n' \
               f'\t- Leakage power {self._leakage_power} W'
