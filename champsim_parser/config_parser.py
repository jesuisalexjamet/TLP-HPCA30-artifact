#!/usr/bin/env python3

from typing import Text

import re

from champsim_parser.filters import BaseFilter


re_uses_sdc = re.compile(r'.*(sdc).*')


def new_caches_parser(dir_path: Text):
    """

    :param dir_path:
    :return:
    """
    struct_conf = {
        'warmup_instructions': 0,
        'simulation_instructions': 0,
        'bin': '',
        'uses_sdc': False,
    }

    path_tokens = dir_path.split('/')[2:]

    # Getting the number of simulation instructions.
    struct_conf['simulation_instructions'] = int(path_tokens[0][:-1])

    # Getting the number of warmup instructions.
    struct_conf['warmup_instructions'] = int(path_tokens[1][:-1])

    # Getting the name of the binary tested.
    struct_conf['bin'] = path_tokens[2]

    # Does this design uses a SDC?
    struct_conf['uses_sdc'] = re_uses_sdc.match(string=struct_conf['bin']) is not None

    return True, struct_conf

def four_sampler_parser(dir_path: Text):
    """

    :type dir_path: Text:
    """
    struct_conf = {
        'warmup_instructions': 0,
        'simulation_instructions': 0,
        'bin': '',
        'features': '',
        'tau_0_0': 0,
        'tau_0_1': 0,
        'tau_0_2': 0,
        'tau_0_3': 0,
        'tau_0_4': 0,
        'tau_1_0': 0,
        'tau_1_1': 0,
        'tau_1_2': 0,
        'tau_1_3': 0,
        'tau_1_4': 0,
        'tau_2_0': 0,
        'tau_2_1': 0,
        'tau_2_2': 0,
        'tau_2_3': 0,
        'tau_2_4': 0,
        'tau_3_0': 0,
        'tau_3_1': 0,
        'tau_3_2': 0,
        'tau_3_3': 0,
        'tau_3_4': 0,
        'table_size': 0,
        'counter_size': 0,
    }

    path_tokens = dir_path.split('/')[2:]

    # Sanity check: the size of the list should be 8, just like the number of parameters.
    if len(path_tokens) != 8:
        # TODO: Do something nasty here!
        pass

    # Getting the number of simulation instructions.
    struct_conf['simulation_instructions'] = int(path_tokens[0][:-1])

    # Getting the number of warmup instructions.
    struct_conf['warmup_instructions'] = int(path_tokens[1][:-1])

    # Getting the name of the binary tested.
    struct_conf['bin'] = path_tokens[2]

    # Getting the set of features used.
    struct_conf['features'] = path_tokens[3]

    # Getting the different threshold.
    struct_conf['tau_0_0'] = int(path_tokens[4])
    struct_conf['tau_0_1'] = int(path_tokens[5])
    struct_conf['tau_0_2'] = int(path_tokens[6])
    struct_conf['tau_0_3'] = int(path_tokens[7])
    struct_conf['tau_0_4'] = int(path_tokens[8])  # As we are not actively using tau_4 we provide it with an unrealistic value.

    struct_conf['tau_1_0'] = int(path_tokens[9])
    struct_conf['tau_1_1'] = int(path_tokens[10])
    struct_conf['tau_1_2'] = int(path_tokens[11])
    struct_conf['tau_1_3'] = int(path_tokens[12])
    struct_conf['tau_1_4'] = int(path_tokens[13])  # As we are not actively using tau_4 we provide it with an unrealistic value.

    struct_conf['tau_2_0'] = int(path_tokens[14])
    struct_conf['tau_2_1'] = int(path_tokens[15])
    struct_conf['tau_2_2'] = int(path_tokens[16])
    struct_conf['tau_2_3'] = int(path_tokens[17])
    struct_conf['tau_2_4'] = int(path_tokens[18])  # As we are not actively using tau_4 we provide it with an unrealistic value.

    struct_conf['tau_3_0'] = int(path_tokens[19])
    struct_conf['tau_3_1'] = int(path_tokens[20])
    struct_conf['tau_3_2'] = int(path_tokens[21])
    struct_conf['tau_3_3'] = int(path_tokens[22])
    struct_conf['tau_3_4'] = int(path_tokens[23])  # As we are not actively using tau_4 we provide it with an unrealistic value.

    # Getting the size of the prediction tables.
    struct_conf['table_size'] = int(path_tokens[-2])

    # Getting the number bits used to code the confidence counters.
    struct_conf['counter_size'] = int(path_tokens[-1][:-4])

    return struct_conf


def multi_sampler_parser(dir_path: Text):
    """

    :type dir_path: Text:
    """
    struct_conf = {
        'warmup_instructions': 0,
        'simulation_instructions': 0,
        'bin': '',
        'features': '',
        'tau_0_0': 0,
        'tau_0_1': 0,
        'tau_0_2': 0,
        'tau_0_3': 0,
        'tau_0_4': 0,
        'tau_1_0': 0,
        'tau_1_1': 0,
        'tau_1_2': 0,
        'tau_1_3': 0,
        'tau_1_4': 0,
        'table_size': 0,
        'counter_size': 0,
    }

    path_tokens = dir_path.split('/')[2:]

    # Sanity check: the size of the list should be 8, just like the number of parameters.
    if len(path_tokens) != 8:
        # TODO: Do something nasty here!
        pass

    # Getting the number of simulation instructions.
    struct_conf['simulation_instructions'] = int(path_tokens[0][:-1])

    # Getting the number of warmup instructions.
    struct_conf['warmup_instructions'] = int(path_tokens[1][:-1])

    # Getting the name of the binary tested.
    struct_conf['bin'] = path_tokens[2]

    # Getting the set of features used.
    struct_conf['features'] = path_tokens[3]

    # Getting the different threshold.
    struct_conf['tau_0_0'] = int(path_tokens[4])
    struct_conf['tau_0_1'] = int(path_tokens[5])
    struct_conf['tau_0_2'] = int(path_tokens[6])
    struct_conf['tau_0_3'] = int(path_tokens[7])
    struct_conf['tau_0_4'] = int(path_tokens[8])  # As we are not actively using tau_4 we provide it with an unrealistic value.

    struct_conf['tau_1_0'] = int(path_tokens[9])
    struct_conf['tau_1_1'] = int(path_tokens[10])
    struct_conf['tau_1_2'] = int(path_tokens[11])
    struct_conf['tau_1_3'] = int(path_tokens[12])
    struct_conf['tau_1_4'] = int(path_tokens[13])  # As we are not actively using tau_4 we provide it with an unrealistic value.

    # Getting the size of the prediction tables.
    struct_conf['table_size'] = int(path_tokens[-2])

    # Getting the number bits used to code the confidence counters.
    struct_conf['counter_size'] = int(path_tokens[-1][:-4])

    return True, struct_conf


def hyperion_coefficient_parser(dir_path: Text):
    """

    :type dir_path: Text:
    """
    struct_conf = {
        'warmup_instructions': 0,
        'simulation_instructions': 0,
        'bin': '',
        'features': '',
        'tau_0': 0,
        'tau_1': 0,
        'tau_2': 0,
        'tau_3': 0,
        'tau_4': 0,
        'table_size': 0,
        'counter_size': 0,
    }

    path_tokens = dir_path.split('/')[2:]

    # Sanity check: the size of the list should be 8, just like the number of parameters.
    if len(path_tokens) != 8:
        # TODO: Do something nasty here!
        pass

    # Getting the number of simulation instructions.
    struct_conf['simulation_instructions'] = int(path_tokens[0][:-1])

    # Getting the number of warmup instructions.
    struct_conf['warmup_instructions'] = int(path_tokens[1][:-1])

    # Getting the name of the binary tested.
    struct_conf['bin'] = path_tokens[2]

    # Getting the set of features used.
    struct_conf['features'] = path_tokens[3]

    # Getting the different threshold.
    struct_conf['tau_0'] = int(path_tokens[4])
    struct_conf['tau_1'] = int(path_tokens[5])
    struct_conf['tau_2'] = int(path_tokens[6])
    struct_conf['tau_3'] = int(path_tokens[7])
    struct_conf['tau_4'] = int(
        path_tokens[8])  # As we are not actively using tau_4 we provide it with an unrealistic value.

    # Getting the size of the prediction tables.
    struct_conf['table_size'] = int(path_tokens[-2])

    # Getting the number bits used to code the confidence counters.
    struct_conf['counter_size'] = int(path_tokens[-1][:-4])

    return struct_conf


class BaseConfigParser:

    def __init__(self, filters = BaseFilter()):
        # A couple of checks on the given filters.
        if isinstance(filters, list):
            for e in filters:
                if not isinstance(e, BaseFilter):
                    raise Exception(f'{e} is not an instance of BaseFilter.')

            self._filters = filters
        else:
            if not isinstance(filters, BaseFilter):
                raise Exception(f'{filters} is not an instance of BaseFilter.')

            self._filters = [filters]

    def __call__(self, dir_path: Text):
        """
        This function should be called anytime one needs to parse the configuration of an experiment.
        It returns both the parsed configuration and a boolean stating if the configuration should be
        taken into account.

        :param dir_path: The path to the results of a single result set. That path contains configuration for that
        result set.
        :return: A tuple made of a boolean and an object containing the parsed configuration of the result set.
        """
        struct_conf, filters_results = {
            'warmup_instructions': 0,
            'simulation_instructions': 0,
            'bin': '',
            'features': '',
            'tau_0_0': 0,
            'tau_0_1': 0,
            'tau_0_2': 0,
            'tau_0_3': 0,
            'tau_0_4': 0,
            'tau_1_0': 0,
            'tau_1_1': 0,
            'tau_1_2': 0,
            'tau_1_3': 0,
            'tau_1_4': 0,
            'table_size': 0,
            'counter_size': 0,
        }, True

        path_tokens = dir_path.split('/')[2:]

        # Sanity check: the size of the list should be 8, just like the number of parameters.
        if len(path_tokens) != 8:
            # TODO: Do something nasty here!
            pass

        # Getting the number of simulation instructions.
        struct_conf['simulation_instructions'] = int(path_tokens[0][:-1])

        # Getting the number of warmup instructions.
        struct_conf['warmup_instructions'] = int(path_tokens[1][:-1])

        # Getting the name of the binary tested.
        struct_conf['bin'] = path_tokens[2]

        # Getting the set of features used.
        struct_conf['features'] = path_tokens[3]

        # Getting the different threshold.
        struct_conf['tau_0_0'] = int(path_tokens[4])
        struct_conf['tau_0_1'] = int(path_tokens[5])
        struct_conf['tau_0_2'] = int(path_tokens[6])
        struct_conf['tau_0_3'] = int(path_tokens[7])
        struct_conf['tau_0_4'] = int(path_tokens[8])  # As we are not actively using tau_4 we provide it with an unrealistic value.

        struct_conf['tau_1_0'] = int(path_tokens[9])
        struct_conf['tau_1_1'] = int(path_tokens[10])
        struct_conf['tau_1_2'] = int(path_tokens[11])
        struct_conf['tau_1_3'] = int(path_tokens[12])
        struct_conf['tau_1_4'] = int(path_tokens[13])  # As we are not actively using tau_4 we provide it with an unrealistic value.

        # Getting the size of the prediction tables.
        struct_conf['table_size'] = int(path_tokens[-2])

        # Getting the number bits used to code the confidence counters.
        struct_conf['counter_size'] = int(path_tokens[-1][:-4])

        # Running filters on the configuration.
        for e in self._filters :
            filters_results |= e(struct_conf)

        return filters_results, struct_conf
