#!/usr/bin/env python3

from _collections import OrderedDict
from typing import Dict, Any, Text

from champsim_parser.energy_modelling.cache_energy_model import CacheEnergyModel


class ResultSet:
    _results: Dict[Text, Any]

    def __init__(self, config):
        self._config = config
        self._results = OrderedDict()

        self._cache_energy_models = {
            'l1d': CacheEnergyModel('l1d', 0.323109, 0.329212, 19.9591),
            'l2c': CacheEnergyModel('l2c', 0.278033, 0.301787, 366.279),
            'llc': CacheEnergyModel('llc', 0.296178, 0.314839, 495.086),
        }

        if self._config['uses_sdc']:
            self._cache_energy_models.update({
                'sdc': CacheEnergyModel('sdc', 0.028119, 0.0296871, 5.67437)
            })

    def sort(self):
        self._results = OrderedDict(sorted(self._results.items(), key=lambda e: e[0]))

    @property
    def config(self):
        return self._config

    def config_math(self, other_config):
        return set(other_config.sets()).issubset(set(self._config.sets()))

    @property
    def cache_energy_models(self):
        return self._cache_energy_models

    def __len__(self):
        return len(self._results)

    def __getitem__(self, item):
        return self._results[item]

    def __setitem__(self, key, value):
        self._results[key] = value

    def __delitem__(self, key):
        del self._results[key]

    def __missing__(self, key):
        return None

    def keys(self):
        return self._results.keys()

    def values(self):
        return self._results.values()

    def items(self):
        return self._results.items()

    # Declaring comparison operators.
    def __eq__(self, other):
        return self._config == other.config

    def __ne__(self, other):
        if other is None:
            return True
        return self._config != other.config

    def __contains__(self, item):
        return item in self._results

    def __call__(self, func, *args):
        """
        This magic method allows the user to call manipulators on a results set instance in order to get a new one.

        Manipulators allowed here, should match a simple pattern which is as follow: the manipulator takes as first
        parameter a result set to be modified as much additional parameters that should be passed to it in order to
        go through its process.

        :param func: The manipulator to be called.
        :param args: A list of additional arguments to be used by the manipulator.
        :return:
        """
        return func(self, *args)


class MultiCoreResultSet:
    """

    """
    def __init__(self, config, mixes):
        self._config = config
        self._mixes = mixes
        self._results = OrderedDict()

    def sort(self):
        self._results = OrderedDict(sorted(self._results.items(), key=lambda e: e[0]))

    @property
    def config(self):
        return self._config

    @property
    def mixes(self):
        return self._mixes

    def config_math(self, other_config):
        return set(other_config.sets()).issubset(set(self._config.sets()))

    def __len__(self):
        return len(self._results)

    def __getitem__(self, item):
        return self._results[item]

    def __setitem__(self, key, value):
        self._results[key] = value

    def __delitem__(self, key):
        del self._results[key]

    def __missing__(self, key):
        return None

    def keys(self):
        return self._results.keys()

    def values(self):
        return self._results.values()

    def items(self):
        return self._results.items()

    # Declaring comparison operators.
    def __eq__(self, other):
        return self._config == other.config

    def __ne__(self, other):
        if other is None:
            return True
        return self._config != other.config

    def __call__(self, func, *args):
        """
        This magic method allows the user to call manipulators on a results set instance in order to get a new one.

        Manipulators allowed here, should match a simple pattern which is as follow: the manipulator takes as first
        parameter a result set to be modified as much additional parameters that should be passed to it in order to
        go through its process.

        :param func: The manipulator to be called.
        :param args: A list of additional arguments to be used by the manipulator.
        :return:
        """
        return func(self, *args)
