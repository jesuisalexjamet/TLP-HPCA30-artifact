#!/usr/bin/env python3

import os
import re

from champsim_parser.result_set import ResultSet
from champsim_parser.experiments import Experiments


class Parser:
    def __init__(self):
        pass

    def parse(self, in_dir, conf_parser, result_parser, func=None):
        """
        This method takes as a parameter a path to a directory that contains ChampSim's simulations output files.
        It will then parse the simulation's configurations along with results contained in the output files.

        :param in_dir: The input directory we have to browse and get results files from.
        :param conf_parser: A function that will be used to generate the simulation's configurations structures.
        :param result_parser: A function that will be used to generate a structured version of the simulation results.
        :return: An array of ResultSet instances, one for each simulation configuration encountered while
        browsing over the provided directory hierarchy.
        """
        results_sets, config_exists = Experiments(), False

        # We start by exploring the provided directory hierarchy.
        for root, dirs, files in os.walk(f'{in_dir}'):
            files = [f for f in files if not f[0] == '.']
            dirs[:] = [d for d in dirs if not d[0] == '.']

            # For every single file in the current directory, we execute both conf_parser and result_parser on its
            # associated file descriptor in order to get the appropriate data.
            for filename in files:
                keep_conf, sim_conf = conf_parser(root)
                found_config = None

                # If the configuration is not mant to be kept, just skipping.
                if not keep_conf:
                    continue

                for e in results_sets.sets:
                    config_exists = e.config == sim_conf

                    if config_exists:
                        found_config = e
                        break

                with open(f'{root}/{filename}', encoding='utf8', errors='ignore') as data_fd:
                    keep_result, sim_data = result_parser(data_fd)

                    if not keep_result:
                        continue

                    # If the config already exist, we just insert a new entry, otherwise
                    # we create a new set of results.
                    if config_exists:
                        found_config[filename] = sim_data
                    else:
                        new_result_set = ResultSet(sim_conf)
                        new_result_set[filename] = sim_data

                        results_sets += new_result_set

        # Computing additional information such as LLC MPKI, etc.
        if func is not None:
            for f in func:
                for e in results_sets.sets:
                    e(f)

        # Sorting the result set for it to look nice.
        for e in results_sets.sets:
            e.sort()

        return results_sets


class MultiCoreParser:
    """

    """

    def __init__(self):
        # Building a couple of regular expressions to be able to parse.
        self._singles_regex = re.compile(r'\w+_singles')
        self._mixes_regex = re.compile(r'\w+_mixes')

        # Just defining attributes for results parsers.
        self._singles_rp = None
        self._mixes_rp = None

        # Defining attributes for the configuration parser.
        self._conf_parser = None

    @property
    def singles_result_parser(self):
        return self._singles_rp

    @singles_result_parser.setter
    def singles_result_parser(self, value):
        self._singles_rp = value

    @property
    def mixes_results_parser(self):
        return self._mixes_rp

    @mixes_results_parser.setter
    def mixes_results_parser(self, value):
        self._mixes_rp = value

    @property
    def configuration_parser(self):
        return self._conf_parser

    @configuration_parser.setter
    def configuration_parser(self, value):
        self._conf_parser = value

    def parse(self, in_dir):
        """

        :param in_dir:
        :param conf_parser:
        :param results_parser:
        :return:
        """
        results_sets, mixes, config_found = Experiments(), dict(), False

        # We start by exploring the provided directory hierarchy.
        for root, dirs, files in os.walk(f'{in_dir}'):
            # If we come across the mixes directory which contains the description of
            # the different mixed used for the purpose of the simulation, we give it a special
            # treatment.
            if os.path.basename(root) == 'mixes':
                mixes = self._parse_mixes(root, files)
            elif self._singles_regex.match(os.path.basename(root)):
                new_result_set = self._parse_single_results(root, files)

                if new_result_set is not None:
                    results_sets += new_result_set
            elif self._mixes_regex.match(os.path.basename(root)):
                new_result_set = self._parse_mix_results(root, files)

                if new_result_set is not None:
                    results_sets += new_result_set

        # Post-conditions (or sanity checks).
        if len(mixes) == 0:
            raise Exception('No mixes descriptors found.')

        # Return statement.
        return mixes, results_sets

    def _parse_single_results(self, root, files):
        """
        This method is responsible for the parsing of result files regarding the single
        executions. Given a directory specified through root and list of files it collects
        data and build a result set that should eventually be added to an experiment result
        set.

        :param root: The directory in which the result files resides.
        :param files: The list of files
        :return:
        """
        keep_config, sim_conf = self._conf_parser(root)

        # If this configuration is not to be kept, we simply return with a dummy value.
        if not keep_config:
            return None

        new_result_set = ResultSet(sim_conf)

        for filename in files:
            with open(os.path.join(root, filename), 'r', encoding='utf8', errors='ignore') as data_fd:
                keep_result, sim_data = self._singles_rp(data_fd)

                if not keep_result:
                    continue

                # We decided to keep that piece of results we add it.
                new_result_set[filename] = sim_data

        return new_result_set

    def _parse_mix_results(self, root, files):
        keep_config, sim_conf = self._conf_parser(root)

        # If this configuration is not to be kept, we simply return with a dummy value.
        if not keep_config:
            return None

        new_result_set = ResultSet(sim_conf)

        for filename in files:
            with open(os.path.join(root, filename), 'r', encoding='utf8', errors='ignore') as data_fd:
                keep_result, sim_data = self._mixes_rp(data_fd)

                if not keep_result:
                    continue

                # We decided to keep that piece of results we add it.
                new_result_set[filename] = sim_data

        return new_result_set

    def _parse_mixes(self, root, files):
        mixes = {}

        for filename in files:
            mix_entry = list()
            with open(os.path.join(root, filename), 'r', encoding='utf8', errors='ignore') as mix_file:
                for line in mix_file.readlines():
                    mix_entry += [line.rstrip()]

            mixes[filename] = mix_entry

        return mixes
