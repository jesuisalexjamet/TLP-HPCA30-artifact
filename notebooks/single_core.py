#!/usr/bin/env python
# coding: utf-8

# In[ ]:

# This is needed to import the analysis module.
import sys
import os
sys.path.append(os.getcwd())

from champsim_parser.result_set.manipulators import get_sim_points
from champsim_parser.result_set.manipulators import apply_simpoint, normalize_llc_distill_cache
from champsim_parser.result_set.manipulators import pairing_multicore_result_sets, \
    compute_multicore_weighted_ipc, compute_multicore_speedup
from champsim_parser.result_parsers import distill_cache_parser, multicore_cache_parser
from champsim_parser.parser import MultiCoreParser
from champsim_parser.config_parser import new_caches_parser
from champsim_parser.experiments.experiments import Experiments
from champsim_parser.parser import Parser
from IPython.display import display
from matplotlib.gridspec import GridSpec
import matplotlib.cm as cm
import matplotlib.pyplot as plt
from matplotlib.ticker import (MultipleLocator, AutoMinorLocator)
import pandas
import numpy as np
from scipy.stats.mstats import gmean
import re
# import sys
# import os
from copy import deepcopy
get_ipython().run_line_magic('matplotlib', 'inline')


def apply_manipulator_to_all(exp, manip, *args):
    output = Experiments()

    for e in exp.sets:
        output += e(manip, *args)

    return output


def exclude_specs(name, entry):
    re_spec = re.compile(r'^(4.*|6.*)')

    return not re_spec.match(name) and mpki_filter(name, entry)


def only_specs06(name, entry):
    re_spec = re.compile(r'^(4.*)')
    return re_spec.match(name) and mpki_filter(name, entry)


def only_specs17(name, entry):
    re_spec = re.compile(r'^(6.*)')
    return re_spec.match(name) and mpki_filter(name, entry)


def only_ligra(name, entry):
    re_spec = re.compile(r'^ligra_.*')
    return re_spec.match(name) and mpki_filter(name, entry)


def exclude_gapbs(name, entry):
    re_gapbs = re.compile(r'^(bc.*|bfs.*|cc.*|pr.*|tc.*|sssp.*)')

    return not re_gapbs.match(name)


def exclude_ligra(name, entry):
    re_ligra = re.compile(r'^ligra_.*')

    return not re_ligra.match(name) and mpki_filter(name, entry)


def only_gapbs(name, entry):
    re_gapbs = re.compile(r'^(bc.*|bfs.*|cc.*|pr.*|tc.*|sssp.*)')

    return re_gapbs.match(name) and mpki_filter(name, entry)


def only_spec(name, entry):
    re_spec = re.compile(r'^(4.*|6.*)')

    return re_spec.match(name) and mpki_filter(name, entry)


def all_workloads(name, entry):
    return mpki_filter(name, entry)


def mpki_filter(name, entry):
    """
    This function is deisgned to filter out workload that are either not significant or
    that show an unreliable behaviour such as the tc.* workloads.

    :param name: The name of the workload.
    :param entry: A structured object (typically a dictionnary) whose entries are data computed
    based on a post-processing of the ChampSim output files.
    :return: A boolean telling if this workload should be used or not.
    """
    # return 'tc' not in name and entry['llc_ref_line_miss_pki'] > 0.0
    return entry['llc_ref_line_miss_pki'] > 1.0


def set_size(width, fraction=1, subplots=(1, 1)):
    """

    :param width:
    :param fraction:
    :return:
    """
    # Width of figure (in pts)
    fig_width_pt = width * fraction

    # Convert from pt to inches.
    inches_per_pt = 1 / 72.27

    # Golden ration to set aesthetic figure height.
    # https://disq.us/p/2940ij3
    golden_ratio = (5 ** (1 / 2) - 1) / 2

    # Figure width in inches
    fig_width_in = fig_width_pt * inches_per_pt
    # Figure height in inches
    fig_height_in = fig_width_in * golden_ratio * (subplots[0] / subplots[1])

    # if width == fig_text_width:
    #     fig_height_in /= 2

    if width == fig_width:
        fig_height_in *= (2.5/5)

    fig_dim = (fig_width_in, fig_height_in)

    return fig_dim


# Creating the result parser.
p = Parser()

# Getting SimPoints data (weights and more).
simpoints_data = get_sim_points('SimPoints/')

# Some configs on matplotlib.
tex_fonts = {
    # Use Latex to write all text.
    'text.usetex': True,
    'font.family': 'serif',
    # Use 10pt font in plots, to match 10pt font in document.
    'axes.labelsize': 8,
    'font.size': 10,
    # Make the legend/label fonts a little smaller.
    'legend.fontsize': 5,
    'legend.handlelength': 1.0,
    'legend.labelspacing': 0.5,
    'legend.columnspacing': 1.0,
    'xtick.labelsize': 10,
    'ytick.labelsize': 10,

    'hatch.linewidth': 0.15,
}

plt.rcParams.update(tex_fonts)

plot_cmp = 'Greys'

# Creating a regular expression to match the trailing ".sdc" at the end of the single-core workloads.
# As it doesn't provide any information we substitute it with an empty string.
sub_re_trailing_sdc = re.compile(r'(.sdc)')
sub_re_trailing_und = re.compile(r'(_)')


# Figure width base on the column width of the Latex document.
fig_width = 252
fig_text_width = 516


# # Single-Core Evaluation

# ## Evaluation for the IPCP prefetcher

# In[ ]:


# Parsing results file containing data relative to simulations comparing designs using no prefetchers what so ever to designs using a prefetcher only in the L1D.
raw_data = p.parse(
    'results/single_core/', new_caches_parser, distill_cache_parser)
raw_data_cpy = deepcopy(raw_data)


# In[ ]:


# Defining the different configurations used to build this plot.
cl_baseline_config, config_list = \
    {'bin': 'baseline_cascade_lake_ipcp'}, [
        {'bin': 'baseline_cascade_lake_ipcp_spp_ppf'}, # 0
        {'bin': 'baseline_cascade_lake_ipcp_hermes_o'}, # 1

        # WIP: Addition of improved designs for the MICRO'23 rebuttals.
        # WIP: This is now design related to the HPCA'30 submission.
        {'bin': 'baseline_cascade_lake_ipcp_tlp_layered_core_l1d_f20_-25'}, # 2

        # Using a design combining SPP-PPF and Hermes-O as a comparison point for prefetcher accuracy.
        {'bin': 'baseline_cascade_lake_ipcp_spp_ppf_hermes_o'}, # 3
        {'bin': 'baseline_cascade_lake_ipcp_iso_prefetcher'}, # 4
        {'bin': 'baseline_cascade_lake_ipcp_hermes_o_double'}, # 5
        {'bin': 'baseline_cascade_lake_no_prefetchers'}, # 6
    ]

# Isolating results set based on the given configurations.
r_cl_base, r_list = \
    raw_data / cl_baseline_config, [
        raw_data / e for e in config_list]

temp_res_set = [r_cl_base]

temp_res_set.extend(r_list)
temp_res_set.append(raw_data_cpy / cl_baseline_config)

# Normalizing...
for e in temp_res_set:
    print(e.sets[0].config)
    normalize_llc_distill_cache(e.sets[0])


# In[ ]:


labels_dict = {
    'baseline_cascade_lake_no_l1d_prefetcher': 'No Prefetcher',
    'baseline_cascade_lake_l1d_filtered_prefetcher': 'TSP',
    'baseline_cascade_lake_double_l1d': 'L1D 64KB',
    'baseline_cascade_lake_ipcp_hermes_o': 'Hermes',
    'baseline_cascade_lake_hermes_o': 'Hermes',
    'baseline_cascade_lake_hermes_o_no_l1d_prefetcher': 'Hermes no L1D Prefetcher',
    'baseline_cascade_lake_spp_ppf': 'PPF',
    'baseline_cascade_lake_topt': 'T-OPT',
    'baseline_cascade_lake': 'Baseline',

    # WIP: Addition of improved designs for the MICRO'23 rebuttals.
    'baseline_cascade_lake_ipcp_tlp_core_l1d_-15_-35_bis': 'Bimodal Hermes',
    'baseline_cascade_lake_ipcp_tlp_layered_core_l1d_f20_-25': 'TLP',

    'baseline_cascade_lake_ipcp_delayed_hermes_o': 'Delayed Hermes',
    'baseline_cascade_lake_ipcp_delayed_tlp': 'Delayed TSP',
    'baseline_cascade_lake_ipcp_tlp_layered_core_l1d': 'Bimodal TSP',

    'baseline_cascade_lake_ipcp_spp_ppf_hermes_o': 'Hermes + PPF',
    'baseline_cascade_lake_ipcp_iso_prefetcher': '2xIPCP',
    'baseline_cascade_lake_ipcp_hermes_o_double': '2xHermes',
    'baseline_cascade_lake_no_prefetchers': 'No Prefetchers',

    'baseline_cascade_lake_ipcp': 'Baseline',
}


# In[ ]:


final_res_set_gapbs = [apply_manipulator_to_all(e, apply_simpoint, temp_res_set[0].sets[0], simpoints_data, only_gapbs)
                       for e in temp_res_set[1:]]
final_res_set_spec = [apply_manipulator_to_all(e, apply_simpoint, temp_res_set[0].sets[0], simpoints_data, only_spec)
                      for e in temp_res_set[1:]]
final_res_set_spec06 = [apply_manipulator_to_all(e, apply_simpoint, temp_res_set[0].sets[0], simpoints_data, only_specs06)
                        for e in temp_res_set[1:]]
final_res_set_spec17 = [apply_manipulator_to_all(e, apply_simpoint, temp_res_set[0].sets[0], simpoints_data, only_specs17)
                        for e in temp_res_set[1:]]
final_res_set_ligra = [apply_manipulator_to_all(e, apply_simpoint, temp_res_set[0].sets[0], simpoints_data, only_ligra)
                       for e in temp_res_set[1:]]
final_res_set_all = [apply_manipulator_to_all(
    e, apply_simpoint, temp_res_set[0].sets[0], simpoints_data, exclude_ligra) for e in temp_res_set[1:]]

speedup_gapbs_keys = [e for e in final_res_set_gapbs[0].sets[0].keys()
                      if e != 'mean']
gapbs_keys = [e for e in final_res_set_gapbs[0].sets[0].keys()
              if e != 'geomean']
speedup_spec_keys = [e for e in final_res_set_spec[0].sets[0].keys()
                     if e != 'mean']
spec_keys = [e for e in final_res_set_spec[0].sets[0].keys()
             if e != 'geomean']
speedup_spec06_keys = [e for e in final_res_set_spec06[0].sets[0].keys()
                       if e != 'mean']
spec06_keys = [e for e in final_res_set_spec06[0].sets[0].keys()
               if e != 'geomean']
speedup_spec17_keys = [e for e in final_res_set_spec17[0].sets[0].keys()
                       if e != 'mean']
spec17_keys = [e for e in final_res_set_spec17[0].sets[0].keys()
               if e != 'geomean']
speedup_ligra_keys = [e for e in final_res_set_ligra[0].sets[0].keys()
                      if e != 'mean']
ligra_keys = [e for e in final_res_set_ligra[0].sets[0].keys()
              if e != 'geomean']
speedup_all_keys = [
    e for e in final_res_set_all[0].sets[0].keys() if e != 'mean']
all_keys = [
    e for e in final_res_set_all[0].sets[0].keys() if e != 'geomean']


workload_sets = [final_res_set_spec, final_res_set_gapbs, final_res_set_all]

print(len(gapbs_keys), len(spec_keys))


# In[ ]:


dict_llc_mpkis_spec = {
    'baseline_llc_mpki': [final_res_set_spec[-1].sets[0][e]['llc_mpki'] for e in spec_keys if e != 'mean'],
}

df_llc_mpki_spec = pandas.DataFrame(
    dict_llc_mpkis_spec, columns=dict_llc_mpkis_spec.keys(), index=[k for k in spec_keys if k != 'mean'])
df_llc_mpki_spec.sort_values(by='baseline_llc_mpki', inplace=True)

# display(df_llc_mpki_spec)

dict_llc_mpki_gapbs = {
    'baseline_llc_mpki': [final_res_set_gapbs[-1].sets[0][e]['llc_mpki'] for e in gapbs_keys if e != 'mean'],
}
df_llc_mpki_gapbs = pandas.DataFrame(
    dict_llc_mpki_gapbs, columns=dict_llc_mpki_gapbs.keys(), index=[k for k in gapbs_keys if k != 'mean'])

df_llc_mpki_gapbs.sort_values(by='baseline_llc_mpki', inplace=True)

# display(df_llc_mpki_gapbs)

# Updating the keys with proper ordering.
speedup_spec_keys, speedup_gapbs_keys = df_llc_mpki_spec.index.to_list(
), df_llc_mpki_gapbs.index.to_list()
spec_keys, gapbs_keys = df_llc_mpki_spec.index.to_list(
), df_llc_mpki_gapbs.index.to_list()


# In[ ]:


dict_caches_mpki = {
    'L1D': [s[-1].sets[0]['mean']['l1d_mpki'] for s in [final_res_set_spec, final_res_set_gapbs, final_res_set_all]],
    'L2C': [s[-1].sets[0]['mean']['l2c_mpki'] for s in [final_res_set_spec, final_res_set_gapbs, final_res_set_all]],
    'LLC': [s[-1].sets[0]['mean']['llc_mpki'] / 2 for s in [final_res_set_spec, final_res_set_gapbs, final_res_set_all]],
}

df_caches_mpki = pandas.DataFrame(
    dict_caches_mpki, columns=dict_caches_mpki.keys(), index=['SPEC', 'GAP', 'ALL']
)

display(df_caches_mpki)


# In[ ]:


# Here is the actual plotting material.
fig_mpkis = plt.figure(
    constrained_layout=True, figsize=set_size(fig_width), dpi=500)
fig_mpkis.tight_layout(pad=0)
gs = GridSpec(1, 1, figure=fig_mpkis)

fig_mpkis = fig_mpkis.add_subplot(
    gs[:])
fig_mpkis.margins(x=0, tight=True)

xticklabels = df_caches_mpki.index.tolist()
xticklabels = [sub_re_trailing_sdc.sub(repl='', string=e) for e in xticklabels]
xticklabels = [sub_re_trailing_und.sub(
    repl=r'\_', string=e) for e in xticklabels]

key_list = df_caches_mpki.columns.to_list()
# key_list = ['hermes_o_pc_based_2k_entries', 'hermes_o_pc_based', 'popet_o', 'hermes_o_perfect']

cat_spacing = 0.1
bar_width, index = (1 - cat_spacing) / \
    len(key_list), np.arange(1, len(xticklabels) + 1)

colors = cm.get_cmap('Greys')(np.linspace(
    0.0, 1.0, len(key_list) + 1, endpoint=True))

for i, (e, c) in enumerate(zip(key_list, colors[1:])):
    fig_mpkis.bar(index + (i - 1) * (bar_width),
                    df_caches_mpki[e], width=bar_width, edgecolor='black', linewidth=0.2, align='center', label=e, color=c)

fig_mpkis.set_xticks(index)
fig_mpkis.set_xticklabels(xticklabels, rotation=0)
# fig_mpkis.set_xticklabels([])
fig_mpkis.grid(
    color='grey', linestyle='-', linewidth=0.25)
fig_mpkis.set_axisbelow(True)

fig_mpkis.set_ylabel(r'MPKI')

fig_mpkis.tick_params(axis='both')
fig_mpkis.tick_params(labeltop=False)

fig_mpkis.legend(loc='upper left', edgecolor='white', fancybox=False, framealpha=0.0,
                 ncol=1,
                #    bbox_to_anchor=(0, 1.0, 1, 0.2),
                   mode='expand')

for tick in fig_mpkis.xaxis.get_major_ticks():
    tick.label1.set_horizontalalignment('center')

fig_mpkis.yaxis.set_major_locator(MultipleLocator(25))
fig_mpkis.yaxis.set_major_formatter('{x:.0f}')

plt.savefig('plots/single_core/single_core_no_pref_mpkis.pdf',
            format='pdf', dpi='figure')
plt.savefig('plots/single_core/single_core_no_pref_mpkis.png',
            format='png', dpi='figure')


# In[ ]:


res_sets, res_keys = [final_res_set_spec, final_res_set_gapbs], [spec_keys, gapbs_keys]
dram_trans_pref_list, dram_trans_list, dram_trans_all_keys = [], [], []

for set, keys in zip(res_sets, res_keys):
    # dram_trans_pref_list.extend([set[0].sets[0][k]['dram']['transactions'] /
    #                        set[-1].sets[0][k]['dram']['transactions'] for k in keys])
    dram_trans_list.extend([set[1].sets[0][k]['dram']['transactions'] /
                           set[-1].sets[0][k]['dram']['transactions'] for k in keys])

    # Adding keys to the list.
    dram_trans_all_keys.extend(keys)

dict_dram_trans = {
    # 'baseline_cascade_lake_ipcp': dram_trans_pref_list,
    'baseline_cascade_lake_ipcp_hermes_o': dram_trans_list,
}

df_dram_trans = pandas.DataFrame(
    dict_dram_trans, columns=dict_dram_trans.keys(), index=dram_trans_all_keys)

df_dram_trans -= 1.0
df_dram_trans *= 100.0

# df_tmp = df_dram_trans[df_dram_trans.index != 'mean'].sort_values(
#     by=df_dram_trans.columns.to_list()[0], axis='rows', inplace=False)
# df_dram_trans = df_tmp

# # Sorting by geomean speed-up.
# df_dram_trans.sort_values(
#     by='mean', axis='columns', inplace=True, ascending=True)
# df_tmp = df_dram_trans[df_dram_trans.index != 'mean'].sort_values(
#     by=df_dram_trans.columns.to_list()[0], axis='rows', inplace=False)
# df_dram_trans = pandas.concat(
#     [df_tmp, df_dram_trans[df_dram_trans.index == 'mean']])

# Creating a DataFrame containing the means for the different benchmark suites.
dram_trans_pref_mean_list, dram_trans_mean_list, mean_keys = [s[1].sets[0]['mean']['dram']['transactions'] / s[-1].sets[0]['mean']['dram']['transactions'] for s in [*res_sets, final_res_set_all]], \
    [s[1].sets[0]['mean']['dram']['transactions'] / s[-1].sets[0]['mean']['dram']['transactions']
                                   for s in [final_res_set_all]], ['AVG']

df_dram_trans_mean = pandas.DataFrame({
    # 'baseline_cascade_lake_ipcp': dram_trans_pref_mean_list,
    'baseline_cascade_lake_ipcp_hermes_o': dram_trans_mean_list,
}, index=mean_keys)

df_dram_trans_mean -= 1.0
df_dram_trans_mean *= 100.0
# del(df_tmp)

# Concatenating the 50 highest values with the means per benchmark suites.
# df_dram_trans = pandas.concat([df_tmp, df_dram_trans_mean])

display(df_dram_trans)
display(df_dram_trans_mean)


# In[ ]:


# Here is the actual plotting material.
fig_dram_trans = plt.figure(
    constrained_layout=True, figsize=set_size(fig_width), dpi=500)
fig_dram_trans.tight_layout(pad=0)
gs = GridSpec(1, 5, figure=fig_dram_trans)

ax_dram_trans, ax_mean = fig_dram_trans.add_subplot(
    gs[0, :4]), fig_dram_trans.add_subplot(gs[0, 4:])
ax_dram_trans.margins(x=0, tight=True)
ax_mean.margins(x=0, tight=True)

xticklabels = df_dram_trans.index.to_list()
xticklabels = [sub_re_trailing_sdc.sub(repl='', string=e) for e in xticklabels]
xticklabels = [sub_re_trailing_und.sub(
    repl=r'\_', string=e) for e in xticklabels]

key_list = df_dram_trans.columns.to_list()
# key_list = ['hermes_o_pc_based', 'popet_o', 'hermes_o_perceptron_pc_pfn']

cat_spacing = 0.25
bar_width, index = (1 - cat_spacing) / \
    len(key_list), np.arange(1, len(xticklabels) + 1)

colors = cm.get_cmap('magma')(np.linspace(
    0.0, 1.0, len(key_list), endpoint=False))

# for i, (e, c) in enumerate(zip(key_list, colors)):
#     ax_dram_trans.bar(index + i * (bar_width) + (cat_spacing / 2),
#                     df_dram_trans[e], width=bar_width, edgecolor='black', linewidth=0.2, align='edge', label=labels_dict[e], color=c)
for i, (e, c) in enumerate(zip(key_list, colors)):
    colors = []

    for k in df_dram_trans.index.to_list():
        # if k[0] == '4' or k[0] == '6':
        #     colors.append('red')
        # else:
        colors.append('black')

    ax_dram_trans.scatter(index + i * (bar_width) + (cat_spacing / 2),
                    df_dram_trans[e],
                    s=5,
                    marker='o',
                    # width=bar_width, edgecolor='black', linewidth=0.2, align='edge',
                    label=labels_dict[e], color=colors)

ax_dram_trans.axvspan(xmin=0, xmax=len(spec_keys), color='grey', alpha=0.25, zorder=-1)

ax_dram_trans.set_xticks(index)
# ax_dram_trans.set_xticklabels(xticklabels, rotation=90, fontsize=5)
ax_dram_trans.set_xticklabels([])
ax_dram_trans.grid(
    color='grey', linestyle='-', linewidth=0.25, axis='y')
ax_dram_trans.grid(True, which='minor', color='grey',
                         linestyle='--', linewidth=0.2, axis='y')
ax_dram_trans.set_axisbelow(True)

ax_dram_trans.set_ylabel('Increase DRAM\nTransactions (\%)')

ax_dram_trans.tick_params(axis='both')
ax_dram_trans.tick_params(labeltop=False)
ax_dram_trans.tick_params(axis='x',
                          which='both',
                          bottom=False,
                          top=False)

for tick in ax_dram_trans.xaxis.get_major_ticks():
    tick.label1.set_horizontalalignment('left')

ax_dram_trans.set_ylim([-30.0, 30.0])

ax_dram_trans.yaxis.set_major_locator(MultipleLocator(30))
ax_dram_trans.yaxis.set_major_formatter('{x:.0f}')
ax_dram_trans.yaxis.set_minor_locator(MultipleLocator(15))
ax_dram_trans.yaxis.set_minor_formatter('{x:.0f}')

# Annotating the benchmark suites on the plots.
ax_dram_trans.annotate('SPEC', (len(spec_keys) / 2, 20), ha='center', va='center', size=7)
ax_dram_trans.annotate('GAP', (len(spec_keys) + len(gapbs_keys) / 2, 20), ha='center', va='center', size=7)

# ax_dram_trans.legend(loc='upper left', edgecolor='white', fancybox=False, framealpha=0.0, fontsize=5)

# Working on the second subplot that will contain the mean for each benchmark suite.
xticklabels = mean_keys
bar_width, index = (1 - cat_spacing) / \
    len(key_list), np.arange(1, len(xticklabels) + 1)

for i, (e, c) in enumerate(zip(key_list, colors)):
    ax_mean.bar(index + i * bar_width + cat_spacing / 2, df_dram_trans_mean[e], width=bar_width, linewidth=0.2, edgecolor='black', align='edge', label=labels_dict[e], color=c)

ax_mean.set_xticks(index)
ax_mean.set_xticklabels([])
ax_mean.bar_label(ax_mean.containers[0], labels=mean_keys, label_type='edge', rotation=0, fontsize=5, padding=3)
ax_mean.set_ylim([0.0, 20.0])

ax_mean.grid(
    color='grey', linestyle='-', linewidth=0.25)
ax_mean.set_axisbelow(True)

plt.savefig('plots/single_core/single_core_ipcp_dram_trans_relative.pdf',
            format='pdf', dpi='figure')
plt.savefig('plots/single_core/single_core_ipcp_dram_trans_relative.png',
            format='png', dpi='figure')


# In[ ]:


res_sets, res_keys = [final_res_set_spec, final_res_set_gapbs], [
    [k for k in speedup_spec_keys if k != 'geomean'], [k for k in speedup_gapbs_keys if k != 'geomean']]
speedup_list, speedup_pref_list, speedup_all_keys = [], [], []

dict_speedup = {
    # 'baseline_cascade_lake_double_l1d': [],
    'baseline_cascade_lake_spp_ppf': [],
    'baseline_cascade_lake_hermes_o': [],
    'baseline_cascade_lake_ipcp_spp_ppf_hermes_o': [],
    # 'baseline_cascade_lake_ipcp_delayed_hermes_o': [],
    # 'baseline_cascade_lake_l1d_filtered_prefetcher': [],
    # 'baseline_cascade_lake_ipcp_delayed_tlp': [],

    # WIP: Addition of improved designs for the MICRO'23 rebuttals.
    # 'baseline_cascade_lake_ipcp_tlp_core_l1d_-15_-35_bis': [],
    # 'baseline_cascade_lake_ipcp_tlp_layered_core_l1d': [],
    'baseline_cascade_lake_ipcp_tlp_layered_core_l1d_f20_-25': [],
    # 'baseline_cascade_lake_ipcp_iso_prefetcher': [],
    # 'baseline_cascade_lake_ipcp_hermes_o_double': [],
    # 'baseline_cascade_lake_no_prefetchers': [],
    # 'baseline_cascade_lake_ipcp_block_prefs': [],
    # 'baseline_cascade_lake_ipcp_slp': [],
}

for set, keys in zip(res_sets, res_keys):
    # speedup_pref_list.extend([set[0].sets[0][k]['speedup'] for k in keys])
    # dict_speedup['baseline_cascade_lake_double_l1d'].extend([set[0].sets[0][k]['speedup'] for k in keys])
    dict_speedup['baseline_cascade_lake_spp_ppf'].extend(
        [set[0].sets[0][k]['speedup'] for k in keys])
    dict_speedup['baseline_cascade_lake_hermes_o'].extend(
        [set[1].sets[0][k]['speedup'] for k in keys])
    dict_speedup['baseline_cascade_lake_ipcp_spp_ppf_hermes_o'].extend(
        [set[3].sets[0][k]['speedup'] for k in keys])
    dict_speedup['baseline_cascade_lake_ipcp_tlp_layered_core_l1d_f20_-25'].extend(
        [set[2].sets[0][k]['speedup'] for k in keys])

    # Adding keys to the list.
    speedup_all_keys.extend(keys)

# for k, v in dict_speedup.items():
#     dict_speedup[k] = sorted(v)

df_speedup_hermes_o = pandas.DataFrame(
    dict_speedup, columns=dict_speedup.keys(), index=speedup_all_keys)

# df_tmp = df_speedup_hermes_o[df_speedup_hermes_o.index != 'mean'].sort_values(
#     by=df_speedup_hermes_o.columns.to_list()[-1], axis='rows', inplace=False)
# df_speedup_hermes_o = df_tmp

df_speedup_hermes_o -= 1.0
df_speedup_hermes_o *= 100.0

# Creating a DataFrame containing the geo-means for the different benchmark suites.
speedup_gmean_list, speedup_pref_gmean_list, gmean_keys = [s[0].sets[0]['geomean']['speedup']
                                                           for s in [*res_sets, final_res_set_all]], \
    [s[0].sets[0]['geomean']['speedup']
     for s in [*res_sets, final_res_set_all]], \
    ['ALL']

df_speedup_gmean = pandas.DataFrame({
    # 'baseline_cascade_lake_ipcp': speedup_pref_gmean_list,
    # 'baseline_cascade_lake_ipcp_hermes_o': speedup_gmean_list,
    # 'baseline_cascade_lake_double_l1d': [s[0].sets[0]['geomean']['speedup'] for s in [*res_sets, final_res_set_all]],
    'baseline_cascade_lake_spp_ppf': gmean(df_speedup_hermes_o['baseline_cascade_lake_spp_ppf'] / 100.0 + 1.0),
    'baseline_cascade_lake_hermes_o': gmean(df_speedup_hermes_o['baseline_cascade_lake_hermes_o'] / 100.0 + 1.0),
    'baseline_cascade_lake_ipcp_spp_ppf_hermes_o': gmean(df_speedup_hermes_o['baseline_cascade_lake_ipcp_spp_ppf_hermes_o'] / 100.0 + 1.0),
    # 'baseline_cascade_lake_ipcp_delayed_hermes_o': gmean(df_speedup_hermes_o['baseline_cascade_lake_ipcp_delayed_hermes_o'] / 100.0 + 1.0),
    # 'baseline_cascade_lake_l1d_filtered_prefetcher': [s[3].sets[0]['geomean']['speedup'] for s in [*res_sets, final_res_set_all]],
    # 'baseline_cascade_lake_ipcp_delayed_tlp': [s[7].sets[0]['geomean']['speedup'] for s in [*res_sets, final_res_set_all]],

    # 'baseline_cascade_lake_ipcp_tlp_core_l1d_-15_-35_bis': [s[4].sets[0]['geomean']['speedup'] for s in [*res_sets, final_res_set_all]],
    # 'baseline_cascade_lake_ipcp_tlp_layered_core_l1d': [s[8].sets[0]['geomean']['speedup'] for s in [*res_sets, final_res_set_all]],
    'baseline_cascade_lake_ipcp_tlp_layered_core_l1d_f20_-25': gmean(df_speedup_hermes_o['baseline_cascade_lake_ipcp_tlp_layered_core_l1d_f20_-25'] / 100.0 + 1.0),
    # 'baseline_cascade_lake_ipcp_iso_prefetcher': gmean(df_speedup_hermes_o['baseline_cascade_lake_ipcp_iso_prefetcher'] / 100.0 + 1.0),
    # 'baseline_cascade_lake_ipcp_hermes_o_double': gmean(df_speedup_hermes_o['baseline_cascade_lake_ipcp_hermes_o_double'] / 100.0 + 1.0),
    # 'baseline_cascade_lake_no_prefetchers': gmean(df_speedup_hermes_o['baseline_cascade_lake_no_prefetchers'] / 100.0 + 1.0),
    # 'baseline_cascade_lake_ipcp_block_prefs': gmean(df_speedup_hermes_o['baseline_cascade_lake_ipcp_block_prefs'] / 100.0 + 1.0),
    # 'baseline_cascade_lake_ipcp_slp': gmean(df_speedup_hermes_o['baseline_cascade_lake_ipcp_slp'] / 100.0 + 1.0),
}, index=gmean_keys)

df_speedup_gmean -= 1.0
df_speedup_gmean *= 100.0

labels_dict.update({
    # 'baseline_cascade_lake_ipcp': 'IPCP',
    'baseline_cascade_lake_ipcp_hermes_o': 'Hermes-O',
})

display(df_speedup_hermes_o)
display(df_speedup_gmean)


# In[ ]:


markers = ['.', 's', 'o', 'x', '>', '<', 'v', '^', 'h', 'D']

# Here is the actual plotting material.
fig_hermes_o_speedup = plt.figure(
    constrained_layout=True, figsize=set_size(fig_width), dpi=500)
fig_hermes_o_speedup.tight_layout(pad=0)
gs = GridSpec(1, 5, figure=fig_hermes_o_speedup)

ax_hermes_o_speedup, ax_hermes_o_gmean = fig_hermes_o_speedup.add_subplot(
    gs[0, :4]), fig_hermes_o_speedup.add_subplot(gs[0, 4:])
ax_hermes_o_speedup.margins(x=0, tight=True)

xticklabels = df_speedup_hermes_o.index.tolist()
xticklabels = [sub_re_trailing_sdc.sub(repl='', string=e) for e in xticklabels]
xticklabels = [sub_re_trailing_und.sub(
    repl=r'\_', string=e) for e in xticklabels]

key_list = df_speedup_hermes_o.columns.to_list()
# key_list = ['hermes_o_pc_based', 'popet_o', 'hermes_o_perceptron_pc_pfn']

cat_spacing = 0.05
bar_width, index = (1 - cat_spacing) / \
    len(key_list), np.arange(1, len(xticklabels) + 1)

colors = cm.get_cmap(plot_cmp)(np.linspace(
    0.0, 1.0, len(key_list), endpoint=True))

# for i, (e, c) in enumerate(zip(key_list, colors)):
#     ax_hermes_o_speedup.bar(index + i * (bar_width) + (cat_spacing / 2),
#                     df_speedup_hermes_o[e], width=bar_width, edgecolor='black', linewidth=0.2, align='edge', label=labels_dict[e], color=c)
for i, (e, c, m) in enumerate(zip(key_list, colors, markers)):
    ax_hermes_o_speedup.scatter(index + i * (bar_width) + (cat_spacing / 2),
                                df_speedup_hermes_o[e],
                                s=5,
                                marker=m,
                                # width=bar_width,
                                edgecolor='black',
                                linewidths=0.5,
                                # align='edge',
                                label=labels_dict[e], color=c)

# Annotating the benchmark suites on the plots.
ax_hermes_o_speedup.annotate(
    'SPEC', (len(spec_keys) / 2, -10), ha='center', va='center', size=7)
ax_hermes_o_speedup.annotate('GAP', (len(
    spec_keys) + len(gapbs_keys) / 2, -10), ha='center', va='center', size=7)

ax_hermes_o_speedup.axvspan(xmin=0, xmax=len(
    spec_keys) + 1, color='grey', alpha=0.25, zorder=-1)

ax_hermes_o_speedup.set_xticks(index)
# ax_hermes_o_speedup.set_xticklabels(xticklabels, rotation=90, fontsize=5)
ax_hermes_o_speedup.set_xticklabels([])
ax_hermes_o_speedup.grid(
    color='grey', linestyle='-', linewidth=0.25, axis='y')
ax_hermes_o_speedup.grid(True, which='minor', color='grey',
                         linestyle='--', linewidth=0.2, axis='y')
ax_hermes_o_speedup.set_axisbelow(True)

ax_hermes_o_speedup.set_ylabel(r'Speedup (\%)', fontsize=8)

ax_hermes_o_speedup.tick_params(axis='both')
ax_hermes_o_speedup.tick_params(labeltop=False)
ax_hermes_o_speedup.tick_params(axis='x',
                                which='both',
                                bottom=False,
                                top=False)

ax_hermes_o_speedup.set_ylim([-15.0, 30.0])

ax_hermes_o_speedup.yaxis.set_major_locator(MultipleLocator(30))
ax_hermes_o_speedup.yaxis.set_major_formatter('{x:.0f}')
ax_hermes_o_speedup.yaxis.set_minor_locator(MultipleLocator(15))
ax_hermes_o_speedup.yaxis.set_minor_formatter('{x:.0f}')

for tick in ax_hermes_o_speedup.xaxis.get_major_ticks():
    tick.label1.set_horizontalalignment('left')

ax_hermes_o_speedup.legend(loc='upper left', edgecolor='white', fancybox=False, framealpha=0.0, ncol=2,
                           fontsize=5
                           )

# Working on the second subplot that will contain the mean for each benchmark suite.
xticklabels = gmean_keys
bar_width, index = (1 - cat_spacing) / \
    len(key_list), np.arange(1, len(xticklabels) + 1)

for i, (e, c) in enumerate(zip(key_list, colors)):
    bars = ax_hermes_o_gmean.bar(index + i * bar_width + cat_spacing / 2,
                                 df_speedup_gmean[e], width=bar_width, linewidth=0.2, edgecolor='black', align='edge', label=labels_dict[e], color=c)

for b, k in zip(ax_hermes_o_gmean.patches, key_list):
    ax_hermes_o_gmean.annotate(labels_dict[k], (b.get_x() + b.get_width() / 2, 8), size=4, rotation=90,
                            #    ha='center',
                               # va='center',
                               # xytext=(0, 10), textcoords='offset points'
                               )

ax_hermes_o_gmean.set_xticks(index)
ax_hermes_o_gmean.set_xticklabels([])
# ax_hermes_o_gmean.bar_label(ax_hermes_o_gmean.containers[-1], labels=gmean_keys, label_type='edge', rotation=90, fontsize=5, padding=3)
ax_hermes_o_gmean.set_ylim([-1.0, 20.0])
ax_hermes_o_gmean.grid(
    color='grey', linestyle='-', linewidth=0.25)
ax_hermes_o_gmean.set_axisbelow(True)

plt.savefig('plots/single_core/single_core_ipcp_evaluation_speedup_alt.pdf',
            format='pdf', dpi='figure')
plt.savefig('plots/single_core/single_core_ipcp_evaluation_speedup_alt.png',
            format='png', dpi='figure')


# In[ ]:


res_sets, res_keys = [final_res_set_spec,
                      final_res_set_gapbs], [spec_keys, gapbs_keys]
dram_trans_pref_list, dram_trans_list, dram_trans_all_keys = [], [], []

dict_dram_trans = {
    'baseline_cascade_lake_spp_ppf': [],
    'baseline_cascade_lake_hermes_o': [],
    'baseline_cascade_lake_ipcp_spp_ppf_hermes_o': [],
    'baseline_cascade_lake_ipcp_tlp_layered_core_l1d_f20_-25': [],
}

for set, keys in zip(res_sets, res_keys):
    # speedup_pref_list.extend([set[0].sets[0][k]['speedup'] for k in keys])
    # dict_dram_trans['baseline_cascade_lake_double_l1d'].extend([set[0].sets[0][k]['dram']['transactions'] / set[-1].sets[0][k]['dram']['transactions'] for k in keys])
    dict_dram_trans['baseline_cascade_lake_spp_ppf'].extend(
        [set[0].sets[0][k]['dram']['transactions'] / set[-1].sets[0][k]['dram']['transactions'] for k in keys])
    dict_dram_trans['baseline_cascade_lake_hermes_o'].extend(
        [set[1].sets[0][k]['dram']['transactions'] / set[-1].sets[0][k]['dram']['transactions'] for k in keys])
    dict_dram_trans['baseline_cascade_lake_ipcp_spp_ppf_hermes_o'].extend(
        [set[3].sets[0][k]['dram']['transactions'] / set[-1].sets[0][k]['dram']['transactions'] for k in keys])
    dict_dram_trans['baseline_cascade_lake_ipcp_tlp_layered_core_l1d_f20_-25'].extend(
        [set[2].sets[0][k]['dram']['transactions'] / set[-1].sets[0][k]['dram']['transactions'] for k in keys])

    # Adding keys to the list.
    dram_trans_all_keys.extend(keys)

df_dram_trans = pandas.DataFrame(
    dict_dram_trans, columns=dict_dram_trans.keys(), index=dram_trans_all_keys)

df_dram_trans -= 1.0
df_dram_trans *= 100.0

# Creating a DataFrame containing the means for the different benchmark suites.
dram_trans_pref_mean_list, dram_trans_mean_list, mean_keys = [s[0].sets[0]['mean']['dram']['transactions'] / s[-1].sets[0]['mean']['dram']['transactions'] for s in [*res_sets, final_res_set_all]], \
    [s[0].sets[0]['mean']['dram']['transactions'] / s[-1].sets[0]['mean']['dram']['transactions']
     for s in [*res_sets, final_res_set_all]], ['ALL']

df_dram_trans_mean = pandas.DataFrame({
    'baseline_cascade_lake_spp_ppf': np.nanmean(df_dram_trans['baseline_cascade_lake_spp_ppf']),
    'baseline_cascade_lake_hermes_o': np.nanmean(df_dram_trans['baseline_cascade_lake_hermes_o']),
    'baseline_cascade_lake_ipcp_spp_ppf_hermes_o': np.nanmean(df_dram_trans['baseline_cascade_lake_ipcp_spp_ppf_hermes_o']),
    'baseline_cascade_lake_ipcp_tlp_layered_core_l1d_f20_-25': np.nanmean(df_dram_trans['baseline_cascade_lake_ipcp_tlp_layered_core_l1d_f20_-25']),
}, index=mean_keys)

display(df_dram_trans)
display(df_dram_trans_mean)


# In[ ]:


markers = ['.', 's', 'o', 'x', '>', '<', 'v', '^', 'h', 'D']

# Here is the actual plotting material.
fig_hermes_o_dram_trans = plt.figure(
    constrained_layout=True, figsize=set_size(fig_width), dpi=500)
fig_hermes_o_dram_trans.tight_layout(pad=0)
gs = GridSpec(1, 5, figure=fig_hermes_o_dram_trans)

ax_hermes_o_dram_trans, ax_hermes_o_dram_trans_mean = fig_hermes_o_dram_trans.add_subplot(
    gs[0, :4]), fig_hermes_o_dram_trans.add_subplot(gs[0, 4:])
ax_hermes_o_dram_trans.margins(x=0, tight=True)

xticklabels = df_dram_trans.index.tolist()
xticklabels = [sub_re_trailing_sdc.sub(repl='', string=e) for e in xticklabels]
xticklabels = [sub_re_trailing_und.sub(
    repl=r'\_', string=e) for e in xticklabels]

key_list = df_dram_trans.columns.to_list()
# key_list = ['hermes_o_pc_based', 'popet_o', 'hermes_o_perceptron_pc_pfn']

cat_spacing = 0.05
bar_width, index = (1 - cat_spacing) / \
    len(key_list), np.arange(1, len(xticklabels) + 1)

colors = cm.get_cmap(plot_cmp)(np.linspace(
    0.0, 1.0, len(key_list), endpoint=True))

# for i, (e, c) in enumerate(zip(key_list, colors)):
#     ax_hermes_o_dram_trans.bar(index + i * (bar_width) + (cat_spacing / 2),
#                     df_speedup_hermes_o[e], width=bar_width, edgecolor='black', linewidth=0.2, align='edge', label=labels_dict[e], color=c)
for i, (e, c, m) in enumerate(zip(key_list, colors, markers)):
    ax_hermes_o_dram_trans.scatter(index + i * (bar_width) + (cat_spacing / 2),
                                   df_dram_trans[e],
                                   s=5,
                                   marker=m,
                                   edgecolor='black',
                                   linewidths=0.5,
                                   # width=bar_width, edgecolor='black', linewidth=0.2, align='edge',
                                   label=labels_dict[e], color=c)

# Annotating the benchmark suites on the plots.
ax_hermes_o_dram_trans.annotate(
    'SPEC', (len(spec_keys) / 2, -75), ha='center', va='center', size=7)
ax_hermes_o_dram_trans.annotate('GAP', (len(
    spec_keys) + len(gapbs_keys) / 2, -75), ha='center', va='center', size=7)

ax_hermes_o_dram_trans.axvspan(xmin=0, xmax=len(
    spec_keys) + 1, color='grey', alpha=0.25, zorder=-1)

ax_hermes_o_dram_trans.set_xticks(index)
# ax_hermes_o_dram_trans.set_xticklabels(xticklabels, rotation=90, fontsize=5)
ax_hermes_o_dram_trans.set_xticklabels([])
ax_hermes_o_dram_trans.grid(
    color='grey', linestyle='-', linewidth=0.25, axis='y')
ax_hermes_o_dram_trans.set_axisbelow(True)

ax_hermes_o_dram_trans.set_ylabel(
    'Increase DRAM\nTransactions (\%)', fontsize=8)

ax_hermes_o_dram_trans.tick_params(axis='both')
ax_hermes_o_dram_trans.tick_params(labeltop=False)
ax_hermes_o_dram_trans.tick_params(axis='x',
                                   which='both',
                                   bottom=False,
                                   top=False)

ax_hermes_o_dram_trans.set_ylim([-125.0, 125.0])

for tick in ax_hermes_o_dram_trans.xaxis.get_major_ticks():
    tick.label1.set_horizontalalignment('left')

ax_hermes_o_dram_trans.legend(loc='upper left', edgecolor='white', fancybox=False, framealpha=0.0, ncol=2,
                              fontsize=5
                              )

# Working on the second subplot that will contain the mean for each benchmark suite.
xticklabels = mean_keys
bar_width, index = (1 - cat_spacing) / \
    len(key_list), np.arange(1, len(xticklabels) + 1)

for i, (e, c) in enumerate(zip(key_list, colors)):
    ax_hermes_o_dram_trans_mean.bar(index + i * bar_width + cat_spacing / 2,
                                    df_dram_trans_mean[e], width=bar_width, linewidth=0.2, edgecolor='black', align='edge', label=labels_dict[e], color=c)

for b, k in zip(ax_hermes_o_dram_trans_mean.patches, key_list):
    ax_hermes_o_dram_trans_mean.annotate(labels_dict[k], (b.get_x() + b.get_width() / 2, 17.5), size=4, rotation=90,
                                         )

ax_hermes_o_dram_trans_mean.yaxis.set_major_locator(MultipleLocator(100))
ax_hermes_o_dram_trans_mean.yaxis.set_major_formatter('{x:.0f}')
ax_hermes_o_dram_trans_mean.yaxis.set_minor_locator(MultipleLocator(25))
ax_hermes_o_dram_trans_mean.yaxis.set_minor_formatter('{x:.0f}')

ax_hermes_o_dram_trans_mean.set_xticks(index)
ax_hermes_o_dram_trans_mean.set_xticklabels([])
ax_hermes_o_dram_trans_mean.set_ylim([-40.0, 100.0])
ax_hermes_o_dram_trans_mean.grid(
    color='grey', linestyle='-', linewidth=0.25)
ax_hermes_o_dram_trans_mean.grid(True, which='minor', color='grey',
                         linestyle='--', linewidth=0.2, axis='y')
ax_hermes_o_dram_trans_mean.set_axisbelow(True)
ax_hermes_o_dram_trans_mean.tick_params(axis='y', which='minor', labelsize=7.5)

plt.savefig('plots/single_core/single_core_ipcp_evaluation_dram_transactions_alt.pdf',
            format='pdf', dpi='figure')
plt.savefig('plots/single_core/single_core_ipcp_evaluation_dram_transactions_alt.png',
            format='png', dpi='figure')


# In[ ]:


dict_l1d_accuracy = {
    'baseline_cascade_lake_spp_ppf': [s[0].sets[0]['mean']['l1d_prefetcher']['accuracy'] for s in workload_sets],
    'baseline_cascade_lake_hermes_o': [s[1].sets[0]['mean']['l1d_prefetcher']['accuracy'] for s in workload_sets],
    'baseline_cascade_lake_ipcp_spp_ppf_hermes_o': [s[3].sets[0]['mean']['l1d_prefetcher']['accuracy'] for s in workload_sets],
    'baseline_cascade_lake_ipcp_tlp_layered_core_l1d_f20_-25': [s[2].sets[0]['mean']['l1d_prefetcher']['accuracy'] for s in workload_sets],
}

df_l1d_accuracy = pandas.DataFrame(
    dict_l1d_accuracy, columns=dict_l1d_accuracy.keys(), index=['SPEC', 'GAP', 'ALL'])

df_l1d_accuracy *= 100.0

display(df_l1d_accuracy)


# In[ ]:


# Here is the actual plotting material.
fig_l1d_pref_accuracy = plt.figure(
    constrained_layout=True, figsize=set_size(fig_width), dpi=500)
fig_l1d_pref_accuracy.tight_layout(pad=0)
gs = GridSpec(1, 1, figure=fig_l1d_pref_accuracy)

fig_l1d_pref_accuracy = fig_l1d_pref_accuracy.add_subplot(
    gs[:])
fig_l1d_pref_accuracy.margins(x=0, tight=True)

xticklabels = df_l1d_accuracy.index.tolist()
xticklabels = [sub_re_trailing_sdc.sub(repl='', string=e) for e in xticklabels]
xticklabels = [sub_re_trailing_und.sub(
    repl=r'\_', string=e) for e in xticklabels]

key_list = df_l1d_accuracy.columns.to_list()
# key_list = ['hermes_o_pc_based_2k_entries', 'hermes_o_pc_based', 'popet_o', 'hermes_o_perfect']

cat_spacing = 0.1
bar_width, index = (1 - cat_spacing) / \
    len(key_list), np.arange(1, len(xticklabels) + 1)

colors = cm.get_cmap(plot_cmp)(np.linspace(
    0.0, 1.0, len(key_list), endpoint=True))

for i, (e, c) in enumerate(zip(key_list, colors)):
    fig_l1d_pref_accuracy.bar(index + (i - 1) * (bar_width),
                              df_l1d_accuracy[e], width=bar_width, edgecolor='black', linewidth=0.2, align='center', label=labels_dict[e], color=c)

fig_l1d_pref_accuracy.set_xticks(index)
fig_l1d_pref_accuracy.set_xticklabels(xticklabels, rotation=0)
# fig_l1d_pref_accuracy.set_xticklabels([])
fig_l1d_pref_accuracy.grid(
    color='grey', linestyle='-', linewidth=0.25)
fig_l1d_pref_accuracy.set_axisbelow(True)

fig_l1d_pref_accuracy.set_ylabel(r'Accuracy (\%)')

fig_l1d_pref_accuracy.tick_params(axis='both')
fig_l1d_pref_accuracy.tick_params(labeltop=False)

fig_l1d_pref_accuracy.set_ylim([0, 100.0])

fig_l1d_pref_accuracy.legend(loc='upper left', edgecolor='white', fancybox=False, framealpha=0.0,
                             ncol=3,
                             fontsize=5,
                             )

for tick in fig_l1d_pref_accuracy.xaxis.get_major_ticks():
    tick.label1.set_horizontalalignment('center')

plt.savefig('plots/single_core/single_core_ipcp_evaluation_l1d_prefetcher_accuracy.pdf',
            format='pdf', dpi='figure')
plt.savefig('plots/single_core/single_core_ipcp_evaluation_l1d_prefetcher_accuracy.png',
            format='png', dpi='figure')


# In[ ]:


dict_split_offchip_mispred_hermes_spec = {
    'offchip_pred_l1d': [final_res_set_spec[1].sets[0][k]['offchip_pred']['miss_hit_l1d'] for k in spec_keys if k != 'mean'],
    'offchip_pred_l2c': [final_res_set_spec[1].sets[0][k]['offchip_pred']['miss_hit_l2c'] for k in spec_keys if k != 'mean'],
    'offchip_pred_l2c_llc': [final_res_set_spec[1].sets[0][k]['offchip_pred']['miss_hit_l2c_llc'] for k in spec_keys if k != 'mean'],
    'offchip_pred_dram': [1 - (final_res_set_spec[1].sets[0][k]['offchip_pred']['miss_hit_l2c_llc'] + final_res_set_spec[1].sets[0][k]['offchip_pred']['miss_hit_l1d'] + final_res_set_spec[1].sets[0][k]['offchip_pred']['miss_hit_l2c']) for k in spec_keys if k != 'mean'],
}

df_split_offchip_mispred_hermes_spec = pandas.DataFrame(
    dict_split_offchip_mispred_hermes_spec, columns=dict_split_offchip_mispred_hermes_spec.keys(), index=[k for k in spec_keys if k != 'mean'])

# # Sorting by geomean speed-up.
# df_split_offchip_mispred_hermes_spec.sort_values(
#     by='geomean', axis='columns', inplace=True)
# df_tmp = df_split_offchip_mispred_hermes_spec[df_split_offchip_mispred_hermes_spec.index != 'mean'].sort_values(
#     by=df_split_offchip_mispred_hermes_spec.columns.to_list()[0], axis='rows', inplace=False)
# df_split_offchip_mispred_hermes_spec = pandas.concat(
#     [df_tmp, df_split_offchip_mispred_hermes_spec[df_split_offchip_mispred_hermes_spec.index == 'mean']])

df_split_offchip_mispred_hermes_spec *= 100.0

# speedup_gapbs_keys = df_split_offchip_mispred_hermes_spec.index.to_list()
# gapbs_keys = speedup_gapbs_keys[:-1] + ['mean']

display(df_split_offchip_mispred_hermes_spec)

# Labels for the plots.
labels_dict = {
    'offchip_pred_l1d': 'L1D',
    'offchip_pred_l2c_llc': 'L2C/LLC',
    'offchip_pred_dram': 'DRAM',
}


# In[ ]:


dict_split_offchip_mispred_hermes_gapbs = {
    'offchip_pred_l1d': [final_res_set_gapbs[2].sets[0][k]['offchip_pred']['miss_hit_l1d'] for k in gapbs_keys if k != 'mean'],
    'offchip_pred_l2c': [final_res_set_gapbs[2].sets[0][k]['offchip_pred']['miss_hit_l2c'] for k in gapbs_keys if k != 'mean'],
    'offchip_pred_l2c_llc': [final_res_set_gapbs[2].sets[0][k]['offchip_pred']['miss_hit_l2c_llc'] for k in gapbs_keys if k != 'mean'],
    'offchip_pred_dram': [1 - (final_res_set_gapbs[2].sets[0][k]['offchip_pred']['miss_hit_l2c_llc'] + final_res_set_gapbs[2].sets[0][k]['offchip_pred']['miss_hit_l1d'] + final_res_set_gapbs[2].sets[0][k]['offchip_pred']['miss_hit_l2c']) for k in gapbs_keys if k != 'mean'],
}

df_split_offchip_mispred_hermes_gapbs = pandas.DataFrame(
    dict_split_offchip_mispred_hermes_gapbs, columns=dict_split_offchip_mispred_hermes_gapbs.keys(), index=[k for k in gapbs_keys if k != 'mean'])

# # Sorting by geomean speed-up.
# df_split_offchip_mispred_hermes_gapbs.sort_values(
#     by='geomean', axis='columns', inplace=True)
# df_tmp = df_split_offchip_mispred_hermes_gapbs[df_split_offchip_mispred_hermes_gapbs.index != 'mean'].sort_values(
#     by=df_split_offchip_mispred_hermes_gapbs.columns.to_list()[0], axis='rows', inplace=False)
# df_split_offchip_mispred_hermes_gapbs = pandas.concat(
#     [df_tmp, df_split_offchip_mispred_hermes_gapbs[df_split_offchip_mispred_hermes_gapbs.index == 'mean']])

df_split_offchip_mispred_hermes_gapbs *= 100.0

# speedup_gapbs_keys = df_split_offchip_mispred_hermes_gapbs.index.to_list()
# gapbs_keys = speedup_gapbs_keys[:-1] + ['mean']

display(df_split_offchip_mispred_hermes_gapbs)

# Labels for the plots.
labels_dict = {
    'offchip_pred_l1d': 'L1D',
    'offchip_pred_l2c': 'L2C',
    'offchip_pred_l2c_llc': 'LLC',
    'offchip_pred_dram': 'DRAM',
}


# In[ ]:


df_split_offchip_mispred_hermes = pandas.concat(
    [df_split_offchip_mispred_hermes_spec, df_split_offchip_mispred_hermes_gapbs])

df_split_offchip_mispred_hermes_mean = pandas.DataFrame({
    'offchip_pred_l1d': [np.mean(df_split_offchip_mispred_hermes['offchip_pred_l1d'])],
    'offchip_pred_l2c': [np.mean(df_split_offchip_mispred_hermes['offchip_pred_l2c'])],
    'offchip_pred_l2c_llc': [np.mean(df_split_offchip_mispred_hermes['offchip_pred_l2c_llc'])],
    'offchip_pred_dram': [np.mean(df_split_offchip_mispred_hermes['offchip_pred_dram'])],
}, index=['AVG'])

# df_split_offchip_mispred_hermes = pandas.concat([df_split_offchip_mispred_hermes, df_split_offchip_mispred_hermes_mean])

display(df_split_offchip_mispred_hermes)
display(df_split_offchip_mispred_hermes_mean)


# In[ ]:


markers = ['.', 's', 'o', 'x', '>', '<', 'v', '^', 'h', 'D']

# Here is the actual plotting material.
fig_hit_miss_l1d = plt.figure(
    constrained_layout=True, figsize=set_size(fig_width), dpi=500)
fig_hit_miss_l1d.tight_layout(pad=0)
gs = GridSpec(nrows=1, ncols=5, figure=fig_hit_miss_l1d)

ax_hit_miss_l1d, ax_hit_miss_l1d_mean = fig_hit_miss_l1d.add_subplot(
    gs[0, :4]), fig_hit_miss_l1d.add_subplot(gs[0, 4:])
xticklabels = df_split_offchip_mispred_hermes.index.tolist()
xticklabels = [sub_re_trailing_sdc.sub(repl='', string=e) for e in xticklabels]
xticklabels = [sub_re_trailing_und.sub(
    repl=r'\_', string=e) for e in xticklabels]

key_list = df_split_offchip_mispred_hermes.columns.to_list()
# key_list = ['hermes_o_pc_based', 'popet_o', 'hermes_o_perceptron_pc_pfn']

cat_spacing = 0.05
bar_width, index = (1 - cat_spacing) / \
    len(key_list), np.arange(0, len(xticklabels))

colors = cm.get_cmap(plot_cmp)(np.linspace(
    0.0, 1.0, len(key_list) + 1, endpoint=True))[1:][::-1]

prev = np.array([0.0 for _ in range(len(df_split_offchip_mispred_hermes))])

for i, (e, c, m) in enumerate(zip(key_list, colors, markers)):
    ax_hit_miss_l1d.bar(index + (cat_spacing / 2),
                        df_split_offchip_mispred_hermes[e],
                        bottom=prev,
                        edgecolor='black',
                        linewidth=0.2,
                        align='edge',
                        label=labels_dict[e], color=c)

    prev += np.array(df_split_offchip_mispred_hermes[e].to_list())

# ax_hit_miss_l1d.axvspan(xmin=0, xmax=len(spec_keys), color='black', alpha=1.0, zorder=-1)
# ax_hit_miss_l1d.axvline(x=len(spec_keys) + 1, color='red', linestyle='--', linewidth=0.35)
# ax_hit_miss_l1d.axvline(x=len(spec_keys) + len(gapbs_keys) + 1, color='red', linestyle='--', linewidth=0.35)


ax_hit_miss_l1d.set_xticks(
    [0, len(spec_keys) + 1, len(spec_keys) + len(gapbs_keys)])
# ax_hit_miss_l1d.set_xticklabels(xticklabels, rotation=90, fontsize=5)
ax_hit_miss_l1d.set_xticklabels(['SPEC', '', 'GAP'], fontsize=5)
ax_hit_miss_l1d.grid(
    color='grey', linestyle='-', linewidth=0.25, axis='y')
ax_hit_miss_l1d.set_axisbelow(True)

ax_hit_miss_l1d.set_ylabel(
    'Off-chip Prediction\nOutcome (\%)', fontsize=8)

ax_hit_miss_l1d.tick_params(axis='both')
ax_hit_miss_l1d.tick_params(labeltop=False)
ax_hit_miss_l1d.tick_params(axis='x',
                            which='both',
                            bottom=True,
                            top=False)

ax_hit_miss_l1d.set_ylim([0.0, 100.0])

# for idx, tick in enumerate(ax_hit_miss_l1d.xaxis.get_major_ticks()):
#     if idx == 0 or idx == 2:
#         tick.set_visible(False)
#         tick.label1.set_visible(True)
#     tick.label1.set_horizontalalignment('center')
ax_hit_miss_l1d.xaxis.get_major_ticks(
)[0].label1.set_horizontalalignment('left')
ax_hit_miss_l1d.xaxis.get_majorticklabels()[0].set_x(len(spec_keys) / 2)
ax_hit_miss_l1d.xaxis.get_major_ticks(
)[-1].label1.set_horizontalalignment('right')
ax_hit_miss_l1d.xaxis.get_majorticklabels(
)[-1].set_x(len(gapbs_keys) + len(spec_keys) / 2)

ax_hit_miss_l1d.legend(loc='upper center', edgecolor='white', fancybox=False, framealpha=0.0, ncol=4,
           bbox_to_anchor=(0.5, 1.2),
           fontsize=5
           )

# Plotting the mean in a seperate subplot.
xticklabels = df_split_offchip_mispred_hermes_mean.index.to_list()
cat_spacing = 0.075
bar_width, index = (1 - cat_spacing) / \
    len(key_list), np.arange(1, len(xticklabels) + 1)

colors = cm.get_cmap(plot_cmp)(np.linspace(
    0.0, 1.0, len(key_list) + 1, endpoint=True))[1:][::-1]
prev = np.array(
    [0.0 for _ in range(len(df_split_offchip_mispred_hermes_mean))])

for i, (e, c, m) in enumerate(zip(key_list, colors, markers)):
    bars = ax_hit_miss_l1d_mean.bar(index + (cat_spacing / 2),
                                    df_split_offchip_mispred_hermes_mean[e],
                                    bottom=prev,
                                    edgecolor='black',
                                    linewidth=0.2,
                                    align='edge',
                                    label=labels_dict[e], color=c)

    prev += df_split_offchip_mispred_hermes_mean[e]

ax_hit_miss_l1d_mean.set_ylim([0.0, 100.0])
ax_hit_miss_l1d_mean.set_xticks(index)
ax_hit_miss_l1d_mean.set_xticklabels([])
ax_hit_miss_l1d_mean.bar_label(ax_hit_miss_l1d_mean.containers[-1], labels=[
                               'AVG'], label_type='edge', rotation=0, fontsize=5, padding=3)
ax_hit_miss_l1d_mean.grid(
    color='grey', linestyle='-', linewidth=0.25)
ax_hit_miss_l1d_mean.set_axisbelow(True)

plt.savefig('plots/single_core/single_core_ipcp_offchip_mispredictions.pdf',
            format='pdf', dpi='figure')
plt.savefig('plots/single_core/single_core_ipcp_offchip_mispredictions.png',
            format='png', dpi='figure')


# In[ ]:


dict_l1d_pref_useless_spec = {
    'l2c': [final_res_set_spec[-1].sets[0][k]['l1d_prefetcher']['useless']['l2c'] for k in spec_keys if k != 'mean'],
    'llc': [final_res_set_spec[-1].sets[0][k]['l1d_prefetcher']['useless']['llc'] for k in spec_keys if k != 'mean'],
    'dram': [final_res_set_spec[-1].sets[0][k]['l1d_prefetcher']['useless']['dram'] for k in spec_keys if k != 'mean'],
}

df_l1d_pref_useless_spec = pandas.DataFrame(
    dict_l1d_pref_useless_spec, columns=dict_l1d_pref_useless_spec.keys(), index=[k for k in spec_keys if k != 'mean'])

display(np.mean(df_l1d_pref_useless_spec))

# # Sorting by geomean speed-up.
# df_l1d_pref_useless_spec.sort_values(
#     by='geomean', axis='columns', inplace=True)
# df_tmp = df_l1d_pref_useless_spec[df_l1d_pref_useless_spec.index != 'mean'].sort_values(
#     by=df_l1d_pref_useless_spec.columns.to_list()[-1], axis='rows', inplace=False)
# df_l1d_pref_useless_spec = pandas.concat(
#     [df_tmp, df_l1d_pref_useless_spec[df_l1d_pref_useless_spec.index == 'mean']])

# speedup_gapbs_keys = df_l1d_pref_useless_spec.index.to_list()
# gapbs_keys = speedup_gapbs_keys[:-1] + ['mean']

# display(df_l1d_pref_useless_spec)

# Labels for the plots.
labels_dict = {
    'l2c': 'L2C',
    'llc': 'LLC',
    'dram': 'DRAM',
}


# In[ ]:


dict_l1d_pref_useless_gapbs = {
    'l2c': [final_res_set_gapbs[-1].sets[0][k]['l1d_prefetcher']['useless']['l2c'] for k in gapbs_keys if k != 'mean'],
    'llc': [final_res_set_gapbs[-1].sets[0][k]['l1d_prefetcher']['useless']['llc'] for k in gapbs_keys if k != 'mean'],
    'dram': [final_res_set_gapbs[-1].sets[0][k]['l1d_prefetcher']['useless']['dram'] for k in gapbs_keys if k != 'mean'],
}

df_l1d_pref_useless_gapbs = pandas.DataFrame(
    dict_l1d_pref_useless_gapbs, columns=dict_l1d_pref_useless_gapbs.keys(), index=[k for k in gapbs_keys if k != 'mean'])

display(np.mean(df_l1d_pref_useless_gapbs))

# # Sorting by geomean speed-up.
# df_l1d_pref_useless_gapbs.sort_values(
#     by='geomean', axis='columns', inplace=True)
# df_tmp = df_l1d_pref_useless_gapbs[df_l1d_pref_useless_gapbs.index != 'mean'].sort_values(
#     by=df_l1d_pref_useless_gapbs.columns.to_list()[-1], axis='rows', inplace=False)
# df_l1d_pref_useless_gapbs = pandas.concat(
#     [df_tmp, df_l1d_pref_useless_gapbs[df_l1d_pref_useless_gapbs.index == 'mean']])

# speedup_gapbs_keys = df_l1d_pref_useless_gapbs.index.to_list()
# gapbs_keys = speedup_gapbs_keys[:-1] + ['mean']

# display(df_l1d_pref_useless_gapbs)

# Labels for the plots.
labels_dict = {
    'l2c': 'L2C',
    'llc': 'LLC',
    'dram': 'DRAM',
}


# In[ ]:


df_l1d_pref_useless = pandas.concat(
    [df_l1d_pref_useless_spec, df_l1d_pref_useless_gapbs])

df_l1d_pref_useless_mean = pandas.DataFrame({
    'l2c': [np.mean(df_l1d_pref_useless['l2c'])],
    'llc': [np.mean(df_l1d_pref_useless['llc'])],
    'dram': [np.mean(df_l1d_pref_useless['dram'])],
}, index=['AVG'])

# df_l1d_pref_useless = pandas.concat([df_l1d_pref_useless, df_l1d_pref_useless_mean])

display(df_l1d_pref_useless)
display(df_l1d_pref_useless_mean)


# In[ ]:


markers = ['.', 's', 'o', 'x', '>', '<', 'v', '^', 'h', 'D']

# Here is the actual plotting material.
fig_hit_miss_l1d = plt.figure(
    constrained_layout=True, figsize=set_size(fig_width), dpi=500)
fig_hit_miss_l1d.tight_layout(pad=0)
gs = GridSpec(nrows=1, ncols=5, figure=fig_hit_miss_l1d)

ax_l1d_useless_loc, ax_l1d_useless_loc_mean = fig_hit_miss_l1d.add_subplot(
    gs[0, :4]), fig_hit_miss_l1d.add_subplot(gs[0, 4:])
ax_l1d_useless_loc.margins(x=0, tight=True)

xticklabels = df_l1d_pref_useless.index.tolist()
xticklabels = [sub_re_trailing_sdc.sub(repl='', string=e) for e in xticklabels]
xticklabels = [sub_re_trailing_und.sub(
    repl=r'\_', string=e) for e in xticklabels]

key_list = df_l1d_pref_useless.columns.to_list()
# key_list = ['hermes_o_pc_based', 'popet_o', 'hermes_o_perceptron_pc_pfn']

cat_spacing = 0.075
bar_width, index = (1 - cat_spacing) / \
    len(key_list), np.arange(1, len(xticklabels) + 1)

colors = cm.get_cmap(plot_cmp)(np.linspace(
    0.0, 1.0, len(key_list) + 1, endpoint=True))[1:][::-1]

prev = np.array([0.0 for _ in range(len(df_l1d_pref_useless))])
bars = None

for i, (e, c, m) in enumerate(zip(key_list, colors, markers)):
    bars = ax_l1d_useless_loc.bar(index + (cat_spacing / 2),
                                  df_l1d_pref_useless[e],
                                  bottom=prev,
                                  edgecolor='black',
                                  linewidth=0.2,
                                  align='edge',
                                  label=labels_dict[e], color=c)

    prev += np.array(df_l1d_pref_useless[e].to_list())

ax_l1d_useless_loc.axvspan(xmin=0, xmax=len(
    df_l1d_pref_useless_spec) + 1, facecolor='grey', alpha=0.25, zorder=-1)
# ax_l1d_useless_loc.axvline(x=len(df_l1d_pref_useless_spec) + len(df_l1d_pref_useless_gapbs) + 1, color='red', linestyle='--', linewidth=0.35)

# Annotating the 5th to last bar of the plot.
ax_l1d_useless_loc.annotate(f'{prev[-4]:.2f}', (bars.patches[-4].get_x() + bars.patches[-4].get_width() / 2 - 3.5, 160
                                                ), ha='center', va='center', textcoords='offset points', xytext=(0, 9), size=4)

ax_l1d_useless_loc.set_xticks(index)
# ax_l1d_useless_loc.set_xticklabels(xticklabels, rotation=90, fontsize=5)
ax_l1d_useless_loc.set_xticklabels([])
ax_l1d_useless_loc.grid(
    color='grey', linestyle='-', linewidth=0.25, axis='y')
ax_l1d_useless_loc.set_axisbelow(True)

ax_l1d_useless_loc.set_ylabel(
    'Prefetches Per Kilo\nInstructions (PPKI)', fontsize=8)

ax_l1d_useless_loc.tick_params(axis='both')
ax_l1d_useless_loc.tick_params(labeltop=False)
ax_l1d_useless_loc.tick_params(axis='x',
                               which='both',
                               bottom=False,
                               top=False)

# ax_l1d_useless_loc.set_yscale('log')
ax_l1d_useless_loc.set_ylim([0.0, 200.0])

for tick in ax_l1d_useless_loc.xaxis.get_major_ticks():
    tick.label1.set_horizontalalignment('left')

ax_l1d_useless_loc.legend(loc='upper center', edgecolor='white', fancybox=False, framealpha=0.0, ncol=3,
                          bbox_to_anchor=(0.5, 1.2),
                          fontsize=5
                          )

# Annotating the benchmark suites on the plots.
ax_l1d_useless_loc.annotate(
    'SPEC', (len(spec_keys) / 2, 125), ha='center', va='center', size=7)
ax_l1d_useless_loc.annotate('GAP', (len(
    spec_keys) + len(gapbs_keys) / 2, 125), ha='center', va='center', size=7)

# Plotting the mean in a seperate subplot.
xticklabels = df_l1d_pref_useless_mean.index.to_list()
cat_spacing = 0.075
bar_width, index = (1 - cat_spacing) / \
    len(key_list), np.arange(1, len(xticklabels) + 1)

colors = cm.get_cmap(plot_cmp)(np.linspace(
    0.0, 1.0, len(key_list) + 1, endpoint=True))[1:][::-1]
prev = np.array([0.0 for _ in range(len(df_l1d_pref_useless_mean))])

for i, (e, c, m) in enumerate(zip(key_list, colors, markers)):
    bars = ax_l1d_useless_loc_mean.bar(index + (cat_spacing / 2),
                                       df_l1d_pref_useless_mean[e],
                                       bottom=prev,
                                       edgecolor='black',
                                       linewidth=0.2,
                                       align='edge',
                                       label=labels_dict[e], color=c)

    prev += df_l1d_pref_useless_mean[e]

ax_l1d_useless_loc_mean.yaxis.set_major_locator(MultipleLocator(75))
ax_l1d_useless_loc_mean.yaxis.set_major_formatter('{x:.0f}')
ax_l1d_useless_loc_mean.yaxis.set_minor_locator(MultipleLocator(25))
ax_l1d_useless_loc_mean.yaxis.set_minor_formatter('{x:.0f}')

ax_l1d_useless_loc_mean.set_ylim([0.0, 75.0])
ax_l1d_useless_loc_mean.set_xticks(index)
ax_l1d_useless_loc_mean.set_xticklabels([])
ax_l1d_useless_loc_mean.bar_label(ax_l1d_useless_loc_mean.containers[-1], labels=[
                                  'AVG'], label_type='edge', rotation=0, fontsize=5, padding=3)
ax_l1d_useless_loc_mean.grid(
    color='grey', linestyle='-', linewidth=0.25)
ax_l1d_useless_loc_mean.grid(True, which='minor', color='grey',
                             linestyle='--', linewidth=0.2, axis='y')
ax_l1d_useless_loc_mean.set_axisbelow(True)

plt.savefig('plots/single_core/single_core_ipcp_l1d_pref_useless.pdf',
            format='pdf', dpi='figure')
plt.savefig('plots/single_core/single_core_ipcp_l1d_pref_useless.png',
            format='png', dpi='figure')


# In[ ]:


dict_l1d_pref_useful_spec = {
    'l2c': [final_res_set_spec[-1].sets[0][k]['l1d_prefetcher']['useful']['l2c'] for k in spec_keys if k != 'mean'],
    'llc': [final_res_set_spec[-1].sets[0][k]['l1d_prefetcher']['useful']['llc'] for k in spec_keys if k != 'mean'],
    'dram': [final_res_set_spec[-1].sets[0][k]['l1d_prefetcher']['useful']['dram'] for k in spec_keys if k != 'mean'],
}

df_l1d_pref_useful_spec = pandas.DataFrame(
    dict_l1d_pref_useful_spec, columns=dict_l1d_pref_useful_spec.keys(), index=[k for k in spec_keys if k != 'mean'])

display(np.mean(df_l1d_pref_useful_spec))


# In[ ]:


dict_l1d_pref_useful_gapbs = {
    'l2c': [final_res_set_gapbs[-1].sets[0][k]['l1d_prefetcher']['useful']['l2c'] for k in gapbs_keys if k != 'mean'],
    'llc': [final_res_set_gapbs[-1].sets[0][k]['l1d_prefetcher']['useful']['llc'] for k in gapbs_keys if k != 'mean'],
    'dram': [final_res_set_gapbs[-1].sets[0][k]['l1d_prefetcher']['useful']['dram'] for k in gapbs_keys if k != 'mean'],
}

df_l1d_pref_useful_gapbs = pandas.DataFrame(
    dict_l1d_pref_useful_gapbs, columns=dict_l1d_pref_useful_gapbs.keys(), index=[k for k in gapbs_keys if k != 'mean'])

display(np.mean(df_l1d_pref_useful_gapbs))


# In[ ]:


df_l1d_pref_useful = pandas.concat(
    [df_l1d_pref_useful_spec, df_l1d_pref_useful_gapbs])

df_l1d_pref_useful_mean = pandas.DataFrame({
    'l2c': [np.mean(df_l1d_pref_useful['l2c'])],
    'llc': [np.mean(df_l1d_pref_useful['llc'])],
    'dram': [np.mean(df_l1d_pref_useful['dram'])],
}, index=['AVG'])

display(df_l1d_pref_useful)
display(df_l1d_pref_useful_mean)


# In[ ]:


markers = ['.', 's', 'o', 'x', '>', '<', 'v', '^', 'h', 'D']

# Here is the actual plotting material.
fig_hit_miss_l1d = plt.figure(
    constrained_layout=True, figsize=set_size(fig_width), dpi=500)
fig_hit_miss_l1d.tight_layout(pad=0)
gs = GridSpec(nrows=1, ncols=5, figure=fig_hit_miss_l1d)

ax_l1d_useful_loc, ax_l1d_useful_loc_mean = fig_hit_miss_l1d.add_subplot(
    gs[0, :4]), fig_hit_miss_l1d.add_subplot(gs[0, 4:])
ax_l1d_useful_loc.margins(x=0, tight=True)

xticklabels = df_l1d_pref_useful.index.tolist()
xticklabels = [sub_re_trailing_sdc.sub(repl='', string=e) for e in xticklabels]
xticklabels = [sub_re_trailing_und.sub(
    repl=r'\_', string=e) for e in xticklabels]

key_list = df_l1d_pref_useful.columns.to_list()
# key_list = ['hermes_o_pc_based', 'popet_o', 'hermes_o_perceptron_pc_pfn']

cat_spacing = 0.075
bar_width, index = (1 - cat_spacing) / \
    len(key_list), np.arange(1, len(xticklabels) + 1)

colors = cm.get_cmap(plot_cmp)(np.linspace(
    0.0, 1.0, len(key_list) + 1, endpoint=True))[1:][::-1]

prev = np.array([0.0 for _ in range(len(df_l1d_pref_useful))])
bars = None

for i, (e, c, m) in enumerate(zip(key_list, colors, markers)):
    bars = ax_l1d_useful_loc.bar(index + (cat_spacing / 2),
                                  df_l1d_pref_useful[e],
                                  bottom=prev,
                                  edgecolor='black',
                                  linewidth=0.2,
                                  align='edge',
                                  label=labels_dict[e], color=c)

    prev += np.array(df_l1d_pref_useful[e].to_list())

ax_l1d_useful_loc.axvspan(xmin=0, xmax=len(
    df_l1d_pref_useful_spec) + 1, facecolor='grey', alpha=0.25, zorder=-1)
# ax_l1d_useful_loc.axvline(x=len(df_l1d_pref_useful_spec) + len(df_l1d_pref_useful_gapbs) + 1, color='red', linestyle='--', linewidth=0.35)

# Annotating the 5th to last bar of the plot.
ax_l1d_useful_loc.annotate(f'{prev[-4]:.2f}', (bars.patches[-4].get_x() + bars.patches[-4].get_width() / 2 - 3.5, 160
                                                ), ha='center', va='center', textcoords='offset points', xytext=(0, 9), size=4)

ax_l1d_useful_loc.set_xticks(index)
# ax_l1d_useful_loc.set_xticklabels(xticklabels, rotation=90, fontsize=5)
ax_l1d_useful_loc.set_xticklabels([])
ax_l1d_useful_loc.grid(
    color='grey', linestyle='-', linewidth=0.25, axis='y')
ax_l1d_useful_loc.set_axisbelow(True)

ax_l1d_useful_loc.set_ylabel(
    'Prefetches Per Kilo\nInstructions (PPKI)', fontsize=8)

ax_l1d_useful_loc.tick_params(axis='both')
ax_l1d_useful_loc.tick_params(labeltop=False)
ax_l1d_useful_loc.tick_params(axis='x',
                               which='both',
                               bottom=False,
                               top=False)

# ax_l1d_useful_loc.set_yscale('log')
ax_l1d_useful_loc.set_ylim([0.0, 10.0])

for tick in ax_l1d_useful_loc.xaxis.get_major_ticks():
    tick.label1.set_horizontalalignment('left')

ax_l1d_useful_loc.legend(loc='upper center', edgecolor='white', fancybox=False, framealpha=0.0, ncol=3,
                          bbox_to_anchor=(0.5, 1.2),
                          fontsize=5
                          )

# Annotating the benchmark suites on the plots.
ax_l1d_useful_loc.annotate(
    'SPEC', (len(spec_keys) / 2, 125), ha='center', va='center', size=7)
ax_l1d_useful_loc.annotate('GAP', (len(
    spec_keys) + len(gapbs_keys) / 2, 125), ha='center', va='center', size=7)

# Plotting the mean in a seperate subplot.
xticklabels = df_l1d_pref_useful_mean.index.to_list()
cat_spacing = 0.075
bar_width, index = (1 - cat_spacing) / \
    len(key_list), np.arange(1, len(xticklabels) + 1)

colors = cm.get_cmap(plot_cmp)(np.linspace(
    0.0, 1.0, len(key_list) + 1, endpoint=True))[1:][::-1]
prev = np.array([0.0 for _ in range(len(df_l1d_pref_useful_mean))])

for i, (e, c, m) in enumerate(zip(key_list, colors, markers)):
    bars = ax_l1d_useful_loc_mean.bar(index + (cat_spacing / 2),
                                       df_l1d_pref_useful_mean[e],
                                       bottom=prev,
                                       edgecolor='black',
                                       linewidth=0.2,
                                       align='edge',
                                       label=labels_dict[e], color=c)

    prev += df_l1d_pref_useful_mean[e]

ax_l1d_useful_loc_mean.yaxis.set_major_locator(MultipleLocator(5))
ax_l1d_useful_loc_mean.yaxis.set_major_formatter('{x:.0f}')
ax_l1d_useful_loc_mean.yaxis.set_minor_locator(MultipleLocator(2.5))
ax_l1d_useful_loc_mean.yaxis.set_minor_formatter('{x:.1f}')

ax_l1d_useful_loc_mean.set_ylim([0.0, 5.0])
ax_l1d_useful_loc_mean.set_xticks(index)
ax_l1d_useful_loc_mean.set_xticklabels([])
ax_l1d_useful_loc_mean.bar_label(ax_l1d_useful_loc_mean.containers[-1], labels=[
                                  'AVG'], label_type='edge', rotation=0, fontsize=5, padding=3)
ax_l1d_useful_loc_mean.grid(
    color='grey', linestyle='-', linewidth=0.25)
ax_l1d_useful_loc_mean.grid(True, which='minor', color='grey',
                             linestyle='--', linewidth=0.2, axis='y')
ax_l1d_useful_loc_mean.set_axisbelow(True)

plt.savefig('plots/single_core/single_core_ipcp_l1d_pref_useful.pdf',
            format='pdf', dpi='figure')
plt.savefig('plots/single_core/single_core_ipcp_l1d_pref_useful.png',
            format='png', dpi='figure')


# ## Evaluation for the Berti prefetcher

# In[ ]:


# Parsing results file containing data relative to simulations comparing designs using no prefetchers what so ever to designs using a prefetcher only in the L1D.
raw_data = p.parse(
    'results/single_core/', new_caches_parser, distill_cache_parser)
raw_data_cpy = deepcopy(raw_data)


# In[ ]:


# Defining the different configurations used to build this plot.
cl_baseline_config, config_list = \
    {'bin': 'baseline_cascade_lake_berti'}, [
        {'bin': 'baseline_cascade_lake_berti_spp_ppf'}, # 0
        {'bin': 'baseline_cascade_lake_berti_hermes_o'}, # 1

        # WIP: Addition of improved designs for the MICRO'23 rebuttals.
        # WIP: This is now design related to the HPCA'30 submission.
        {'bin': 'baseline_cascade_lake_berti_tlp_layered_core_l1d_f20_-25'}, # 2

        # Using a design combining SPP-PPF and Hermes-O as a comparison point for prefetcher accuracy.
        {'bin': 'baseline_cascade_lake_berti_spp_ppf_hermes_o'}, # 3
        {'bin': 'baseline_cascade_lake_berti_iso_prefetcher'}, # 4
        {'bin': 'baseline_cascade_lake_berti_hermes_o_double'}, # 5
        {'bin': 'baseline_cascade_lake_no_prefetchers'}, # 6
    ]

# Isolating results set based on the given configurations.
r_cl_base, r_list = \
    raw_data / cl_baseline_config, [
        raw_data / e for e in config_list]

temp_res_set = [r_cl_base]

temp_res_set.extend(r_list)
temp_res_set.append(raw_data_cpy / cl_baseline_config)

# Normalizing...
for e in temp_res_set:
    print(e.sets[0].config)
    normalize_llc_distill_cache(e.sets[0])


# In[ ]:


final_res_set_gapbs = [apply_manipulator_to_all(e, apply_simpoint, temp_res_set[0].sets[0], simpoints_data, only_gapbs)
                       for e in temp_res_set[1:]]
final_res_set_spec = [apply_manipulator_to_all(e, apply_simpoint, temp_res_set[0].sets[0], simpoints_data, only_spec)
                      for e in temp_res_set[1:]]
final_res_set_spec06 = [apply_manipulator_to_all(e, apply_simpoint, temp_res_set[0].sets[0], simpoints_data, only_specs06)
                        for e in temp_res_set[1:]]
final_res_set_spec17 = [apply_manipulator_to_all(e, apply_simpoint, temp_res_set[0].sets[0], simpoints_data, only_specs17)
                        for e in temp_res_set[1:]]
final_res_set_ligra = [apply_manipulator_to_all(e, apply_simpoint, temp_res_set[0].sets[0], simpoints_data, only_ligra)
                       for e in temp_res_set[1:]]
final_res_set_all = [apply_manipulator_to_all(
    e, apply_simpoint, temp_res_set[0].sets[0], simpoints_data, exclude_ligra) for e in temp_res_set[1:]]

speedup_gapbs_keys = [e for e in final_res_set_gapbs[0].sets[0].keys()
                      if e != 'mean']
gapbs_keys = [e for e in final_res_set_gapbs[0].sets[0].keys()
              if e != 'geomean']
speedup_spec_keys = [e for e in final_res_set_spec[0].sets[0].keys()
                     if e != 'mean']
spec_keys = [e for e in final_res_set_spec[0].sets[0].keys()
             if e != 'geomean']
speedup_spec06_keys = [e for e in final_res_set_spec06[0].sets[0].keys()
                       if e != 'mean']
spec06_keys = [e for e in final_res_set_spec06[0].sets[0].keys()
               if e != 'geomean']
speedup_spec17_keys = [e for e in final_res_set_spec17[0].sets[0].keys()
                       if e != 'mean']
spec17_keys = [e for e in final_res_set_spec17[0].sets[0].keys()
               if e != 'geomean']
speedup_ligra_keys = [e for e in final_res_set_ligra[0].sets[0].keys()
                      if e != 'mean']
ligra_keys = [e for e in final_res_set_ligra[0].sets[0].keys()
              if e != 'geomean']
speedup_all_keys = [
    e for e in final_res_set_all[0].sets[0].keys() if e != 'mean']
all_keys = [
    e for e in final_res_set_all[0].sets[0].keys() if e != 'geomean']


workload_sets = [final_res_set_spec, final_res_set_gapbs, final_res_set_all]


# In[ ]:


labels_dict = {
    'baseline_cascade_lake_no_l1d_prefetcher': 'No Prefetcher',
    'baseline_cascade_lake_l1d_filtered_prefetcher': 'TSP',
    'baseline_cascade_lake_double_l1d': 'L1D 64KB',
    'baseline_cascade_lake_hermes_o': 'Hermes',
    'baseline_cascade_lake_hermes_o_no_l1d_prefetcher': 'Hermes no L1D Prefetcher',
    'baseline_cascade_lake_spp_ppf': 'PPF',
    'baseline_cascade_lake_topt': 'T-OPT',
    'baseline_cascade_lake': 'Baseline',

    # WIP: Addition of improved designs for the MICRO'23 rebuttals.
    'baseline_cascade_lake_berti_tlp_core_l1d_-15_-35_bis': 'Selective Delay Hermes',
    'baseline_cascade_lake_berti_tlp_layered_core_l1d_f20_-25': 'TLP',

    'baseline_cascade_lake_berti_delayed_hermes_o': 'Delayed Hermes',
    'baseline_cascade_lake_berti_delayed_tlp': 'Delayed TSP',
    'baseline_cascade_lake_berti_tlp_layered_core_l1d': 'Selective Delay TSP',

    'baseline_cascade_lake_berti_spp_ppf_hermes_o': 'Hermes + PPF',
    'baseline_cascade_lake_berti_iso_prefetcher': '2xBerti',
    'baseline_cascade_lake_berti_hermes_o_double': '2xHermes',
    'baseline_cascade_lake_no_prefetchers': 'No Prefetchers',
    'baseline_cascade_lake_berti': 'Baseline',
}


# In[ ]:


dict_llc_mpkis_spec = {
    'baseline_llc_mpki': [final_res_set_spec[-1].sets[0][e]['llc_mpki'] for e in spec_keys if e != 'mean'],
}

df_llc_mpki_spec = pandas.DataFrame(
    dict_llc_mpkis_spec, columns=dict_llc_mpkis_spec.keys(), index=[k for k in spec_keys if k != 'mean'])
df_llc_mpki_spec.sort_values(by='baseline_llc_mpki', inplace=True)

# display(df_llc_mpki_spec)

dict_llc_mpki_gapbs = {
    'baseline_llc_mpki': [final_res_set_gapbs[-1].sets[0][e]['llc_mpki'] for e in gapbs_keys if e != 'mean'],
}
df_llc_mpki_gapbs = pandas.DataFrame(
    dict_llc_mpki_gapbs, columns=dict_llc_mpki_gapbs.keys(), index=[k for k in gapbs_keys if k != 'mean'])

df_llc_mpki_gapbs.sort_values(by='baseline_llc_mpki', inplace=True)

# display(df_llc_mpki_gapbs)

# Updating the keys with proper ordering.
speedup_spec_keys, speedup_gapbs_keys = df_llc_mpki_spec.index.to_list(
), df_llc_mpki_gapbs.index.to_list()
spec_keys, gapbs_keys = df_llc_mpki_spec.index.to_list(
), df_llc_mpki_gapbs.index.to_list()


# In[ ]:


res_sets, res_keys = [final_res_set_spec, final_res_set_gapbs], [
    speedup_spec_keys, speedup_gapbs_keys]
speedup_list, speedup_pref_list, speedup_all_keys = [], [], []

dict_speedup = {
    # 'baseline_cascade_lake_double_l1d': [],
    'baseline_cascade_lake_spp_ppf': [],
    'baseline_cascade_lake_hermes_o': [],
    'baseline_cascade_lake_berti_spp_ppf_hermes_o': [],
    'baseline_cascade_lake_berti_tlp_layered_core_l1d_f20_-25': [],
}

for set, keys in zip(res_sets, res_keys):
    dict_speedup['baseline_cascade_lake_spp_ppf'].extend(
        [set[0].sets[0][k]['speedup'] for k in keys])
    dict_speedup['baseline_cascade_lake_hermes_o'].extend(
        [set[1].sets[0][k]['speedup'] for k in keys])
    dict_speedup['baseline_cascade_lake_berti_spp_ppf_hermes_o'].extend(
        [set[3].sets[0][k]['speedup'] for k in keys])
    dict_speedup['baseline_cascade_lake_berti_tlp_layered_core_l1d_f20_-25'].extend(
        [set[2].sets[0][k]['speedup'] for k in keys])

    # Adding keys to the list.
    speedup_all_keys.extend(keys)

# for k, v in dict_speedup.items():
#     dict_speedup[k] = sorted(v)

df_speedup_hermes_o = pandas.DataFrame(
    dict_speedup, columns=dict_speedup.keys(), index=speedup_all_keys)

# df_tmp = df_speedup_hermes_o[df_speedup_hermes_o.index != 'mean'].sort_values(
#     by=df_speedup_hermes_o.columns.to_list()[0], axis='rows', inplace=False)
# df_speedup_hermes_o = df_tmp

df_speedup_hermes_o -= 1.0
df_speedup_hermes_o *= 100.0

# Creating a DataFrame containing the geo-means for the different benchmark suites.
speedup_gmean_list, speedup_pref_gmean_list, gmean_keys = [s[0].sets[0]['geomean']['speedup']
                                                           for s in [*res_sets, final_res_set_all]], \
    [s[0].sets[0]['geomean']['speedup']
     for s in [*res_sets, final_res_set_all]], \
    ['ALL']

df_speedup_gmean = pandas.DataFrame({
    # 'baseline_cascade_lake_ipcp': speedup_pref_gmean_list,
    # 'baseline_cascade_lake_ipcp_hermes_o': speedup_gmean_list,
    # 'baseline_cascade_lake_double_l1d': [s[1].sets[0]['geomean']['speedup'] for s in [*res_sets, final_res_set_all]],
    'baseline_cascade_lake_spp_ppf': gmean(df_speedup_hermes_o['baseline_cascade_lake_spp_ppf'] / 100.0 + 1.0, nan_policy='omit'),
    'baseline_cascade_lake_hermes_o': gmean(df_speedup_hermes_o['baseline_cascade_lake_hermes_o'] / 100.0 + 1.0, nan_policy='omit'),
    'baseline_cascade_lake_berti_spp_ppf_hermes_o': gmean(df_speedup_hermes_o['baseline_cascade_lake_berti_spp_ppf_hermes_o'] / 100.0 + 1.0, nan_policy='omit'),
    'baseline_cascade_lake_berti_tlp_layered_core_l1d_f20_-25': gmean(df_speedup_hermes_o['baseline_cascade_lake_berti_tlp_layered_core_l1d_f20_-25'] / 100.0 + 1.0, nan_policy='omit'),
}, index=gmean_keys)

df_speedup_gmean -= 1.0
df_speedup_gmean *= 100.0

labels_dict.update({
    # 'baseline_cascade_lake_ipcp': 'IPCP',
    'baseline_cascade_lake_ipcp_hermes_o': 'Hermes-O',
    'baseline_cascade_lake_berti_block_prefs': 'Block Prefs',
    'baseline_cascade_lake_berti_slp': 'SLP',
})

display(df_speedup_hermes_o)
display(df_speedup_gmean)


# In[ ]:


markers = ['.', 's', 'o', 'x', '>', '<', 'v', '^', 'h', 'D']

# Here is the actual plotting material.
fig_hermes_o_speedup = plt.figure(
    constrained_layout=True, figsize=set_size(fig_width), dpi=500)
fig_hermes_o_speedup.tight_layout(pad=0)
gs = GridSpec(1, 5, figure=fig_hermes_o_speedup)

ax_hermes_o_speedup, ax_hermes_o_gmean = fig_hermes_o_speedup.add_subplot(
    gs[0, :4]), fig_hermes_o_speedup.add_subplot(gs[0, 4:])
ax_hermes_o_speedup.margins(x=0, tight=True)

xticklabels = df_speedup_hermes_o.index.tolist()
xticklabels = [sub_re_trailing_sdc.sub(repl='', string=e) for e in xticklabels]
xticklabels = [sub_re_trailing_und.sub(
    repl=r'\_', string=e) for e in xticklabels]

key_list = df_speedup_hermes_o.columns.to_list()
# key_list = ['hermes_o_pc_based', 'popet_o', 'hermes_o_perceptron_pc_pfn']

cat_spacing = 0.05
bar_width, index = (1 - cat_spacing) / \
    len(key_list), np.arange(1, len(xticklabels) + 1)

colors = cm.get_cmap(plot_cmp)(np.linspace(
    0.0, 1.0, len(key_list), endpoint=True))

# for i, (e, c) in enumerate(zip(key_list, colors)):
#     ax_hermes_o_speedup.bar(index + i * (bar_width) + (cat_spacing / 2),
#                     df_speedup_hermes_o[e], width=bar_width, edgecolor='black', linewidth=0.2, align='edge', label=labels_dict[e], color=c)
for i, (e, c, m) in enumerate(zip(key_list, colors, markers)):
    ax_hermes_o_speedup.scatter(index + i * (bar_width) + (cat_spacing / 2),
                                df_speedup_hermes_o[e],
                                s=5,
                                marker=m,
                                edgecolor='black',
                                linewidths=0.5,
                                # width=bar_width, edgecolor='black', linewidth=0.2, align='edge',
                                label=labels_dict[e], color=c)

# Annotating the benchmark suites on the plots.
ax_hermes_o_speedup.annotate(
    'SPEC', (len(spec_keys) / 2, -15), ha='center', va='center', size=7)
ax_hermes_o_speedup.annotate('GAP', (len(
    spec_keys) + len(gapbs_keys) / 2, -15), ha='center', va='center', size=7)

ax_hermes_o_speedup.axvspan(xmin=0, xmax=len(
    spec_keys) + 1, color='grey', alpha=0.25, zorder=-1)

ax_hermes_o_speedup.set_xticks(index)
# ax_hermes_o_speedup.set_xticklabels(xticklabels, rotation=90, fontsize=5)
ax_hermes_o_speedup.set_xticklabels([])
ax_hermes_o_speedup.grid(
    color='grey', linestyle='-', linewidth=0.25, axis='y')
ax_hermes_o_speedup.grid(True, which='minor', color='grey',
                         linestyle='--', linewidth=0.2, axis='y')
ax_hermes_o_speedup.set_axisbelow(True)

ax_hermes_o_speedup.set_ylabel(r'Speedup (\%)', fontsize=8)

ax_hermes_o_speedup.tick_params(axis='both')
ax_hermes_o_speedup.tick_params(labeltop=False)
ax_hermes_o_speedup.tick_params(axis='x',
                                which='both',
                                bottom=False,
                                top=False)

ax_hermes_o_speedup.set_ylim([-20.0, 40.0])

ax_hermes_o_speedup.yaxis.set_major_locator(MultipleLocator(40))
ax_hermes_o_speedup.yaxis.set_major_formatter('{x:.0f}')
ax_hermes_o_speedup.yaxis.set_minor_locator(MultipleLocator(20))
ax_hermes_o_speedup.yaxis.set_minor_formatter('{x:.0f}')

for tick in ax_hermes_o_speedup.xaxis.get_major_ticks():
    tick.label1.set_horizontalalignment('left')

ax_hermes_o_speedup.legend(loc='upper left', edgecolor='white', fancybox=False, framealpha=0.0, ncol=2,
                           fontsize=5
                           )

# Working on the second subplot that will contain the mean for each benchmark suite.
xticklabels = gmean_keys
bar_width, index = (1 - cat_spacing) / \
    len(key_list), np.arange(1, len(xticklabels) + 1)

for i, (e, c) in enumerate(zip(key_list, colors)):
    ax_hermes_o_gmean.bar(index + i * bar_width + cat_spacing / 2,
                          df_speedup_gmean[e], width=bar_width, linewidth=0.2, edgecolor='black', align='edge', label=labels_dict[e], color=c)

for b, k in zip(ax_hermes_o_gmean.patches, key_list):
    ax_hermes_o_gmean.annotate(labels_dict[k], (b.get_x() + b.get_width() / 2, 10), size=4, rotation=90,
                            #    ha='center',
                               # va='center',
                               # xytext=(0, 10), textcoords='offset points'
                               )

ax_hermes_o_gmean.set_xticks(index)
ax_hermes_o_gmean.set_xticklabels([])
# ax_hermes_o_gmean.bar_label(ax_hermes_o_gmean.containers[-1], labels=gmean_keys, label_type='edge', rotation=90, fontsize=5, padding=3)
ax_hermes_o_gmean.set_ylim([0.0, 25.0])
ax_hermes_o_gmean.grid(
    color='grey', linestyle='-', linewidth=0.25)
ax_hermes_o_gmean.set_axisbelow(True)

plt.savefig('plots/single_core/single_core_berti_evaluation_speedup_alt.pdf',
            format='pdf', dpi='figure')
plt.savefig('plots/single_core/single_core_berti_evaluation_speedup_alt.png',
            format='png', dpi='figure')


# In[ ]:


res_sets, res_keys = [final_res_set_spec,
                      final_res_set_gapbs], [spec_keys, gapbs_keys]
dram_trans_pref_list, dram_trans_list, dram_trans_all_keys = [], [], []

dict_dram_trans = {
    'baseline_cascade_lake_spp_ppf': [],
    'baseline_cascade_lake_hermes_o': [],
    'baseline_cascade_lake_berti_spp_ppf_hermes_o': [],
    'baseline_cascade_lake_berti_tlp_layered_core_l1d_f20_-25': [],
}

for set, keys in zip(res_sets, res_keys):
    dict_dram_trans['baseline_cascade_lake_spp_ppf'].extend(
        [set[0].sets[0][k]['dram']['transactions'] / set[-1].sets[0][k]['dram']['transactions'] for k in keys])
    dict_dram_trans['baseline_cascade_lake_hermes_o'].extend(
        [set[1].sets[0][k]['dram']['transactions'] / set[-1].sets[0][k]['dram']['transactions'] for k in keys])
    dict_dram_trans['baseline_cascade_lake_berti_spp_ppf_hermes_o'].extend(
        [set[3].sets[0][k]['dram']['transactions'] / set[-1].sets[0][k]['dram']['transactions'] for k in keys])
    dict_dram_trans['baseline_cascade_lake_berti_tlp_layered_core_l1d_f20_-25'].extend(
        [set[2].sets[0][k]['dram']['transactions'] / set[-1].sets[0][k]['dram']['transactions'] for k in keys])

    # Adding keys to the list.
    dram_trans_all_keys.extend(keys)

df_dram_trans = pandas.DataFrame(
    dict_dram_trans, columns=dict_dram_trans.keys(), index=dram_trans_all_keys)

df_dram_trans -= 1.0
df_dram_trans *= 100.0

# Creating a DataFrame containing the means for the different benchmark suites.
dram_trans_pref_mean_list, dram_trans_mean_list, mean_keys = [s[0].sets[0]['mean']['dram']['transactions'] / s[-1].sets[0]['mean']['dram']['transactions'] for s in [*res_sets, final_res_set_all]], \
    [s[0].sets[0]['mean']['dram']['transactions'] / s[-1].sets[0]['mean']['dram']['transactions']
     for s in [*res_sets, final_res_set_all]], ['ALL']

df_dram_trans_mean = pandas.DataFrame({
    'baseline_cascade_lake_spp_ppf': np.nanmean(df_dram_trans['baseline_cascade_lake_spp_ppf']),
    'baseline_cascade_lake_hermes_o': np.nanmean(df_dram_trans['baseline_cascade_lake_hermes_o']),
    'baseline_cascade_lake_berti_spp_ppf_hermes_o': np.nanmean(df_dram_trans['baseline_cascade_lake_berti_spp_ppf_hermes_o']),
    'baseline_cascade_lake_berti_tlp_layered_core_l1d_f20_-25': np.nanmean(df_dram_trans['baseline_cascade_lake_berti_tlp_layered_core_l1d_f20_-25']),
}, index=mean_keys)

display(np.nanmean(df_dram_trans['baseline_cascade_lake_spp_ppf']))
display(df_dram_trans_mean)


# In[ ]:


markers = ['.', 's', 'o', 'x', '>', '<', 'v', '^', 'h', 'D']

# Here is the actual plotting material.
fig_hermes_o_dram_trans = plt.figure(
    constrained_layout=True, figsize=set_size(fig_width), dpi=500)
fig_hermes_o_dram_trans.tight_layout(pad=0)
gs = GridSpec(1, 5, figure=fig_hermes_o_dram_trans)

ax_hermes_o_dram_trans, ax_hermes_o_dram_trans_mean = fig_hermes_o_dram_trans.add_subplot(
    gs[0, :4]), fig_hermes_o_dram_trans.add_subplot(gs[0, 4:])
ax_hermes_o_dram_trans.margins(x=0, tight=True)

xticklabels = df_dram_trans.index.tolist()
xticklabels = [sub_re_trailing_sdc.sub(repl='', string=e) for e in xticklabels]
xticklabels = [sub_re_trailing_und.sub(
    repl=r'\_', string=e) for e in xticklabels]

key_list = df_dram_trans.columns.to_list()
# key_list = ['hermes_o_pc_based', 'popet_o', 'hermes_o_perceptron_pc_pfn']

cat_spacing = 0.05
bar_width, index = (1 - cat_spacing) / \
    len(key_list), np.arange(1, len(xticklabels) + 1)

colors = cm.get_cmap(plot_cmp)(np.linspace(
    0.0, 1.0, len(key_list), endpoint=True))

# for i, (e, c) in enumerate(zip(key_list, colors)):
#     ax_hermes_o_dram_trans.bar(index + i * (bar_width) + (cat_spacing / 2),
#                     df_speedup_hermes_o[e], width=bar_width, edgecolor='black', linewidth=0.2, align='edge', label=labels_dict[e], color=c)
for i, (e, c, m) in enumerate(zip(key_list, colors, markers)):
    ax_hermes_o_dram_trans.scatter(index + i * (bar_width) + (cat_spacing / 2),
                                   df_dram_trans[e],
                                   s=5,
                                   marker=m,
                                   edgecolor='black',
                                   linewidths=0.5,
                                   # width=bar_width, edgecolor='black', linewidth=0.2, align='edge',
                                   label=labels_dict[e], color=c)

ax_hermes_o_dram_trans.axvspan(xmin=0, xmax=len(
    spec_keys) + 1, color='grey', alpha=0.25, zorder=-1)

# Annotating the benchmark suites on the plots.
ax_hermes_o_dram_trans.annotate(
    'SPEC', (len(spec_keys) / 2, -75), ha='center', va='center', size=7)
ax_hermes_o_dram_trans.annotate('GAP', (len(
    spec_keys) + len(gapbs_keys) / 2, -75), ha='center', va='center', size=7)

ax_hermes_o_dram_trans.set_xticks(index)
# ax_hermes_o_dram_trans.set_xticklabels(xticklabels, rotation=90, fontsize=5)
ax_hermes_o_dram_trans.set_xticklabels([])
ax_hermes_o_dram_trans.grid(
    color='grey', linestyle='-', linewidth=0.25, axis='y')
ax_hermes_o_dram_trans.grid(True, which='minor', color='grey',
                            linestyle='--', linewidth=0.2, axis='y')
ax_hermes_o_dram_trans.set_axisbelow(True)

ax_hermes_o_dram_trans.set_ylabel(
    'Increase DRAM\nTransactions (\%)', fontsize=8)

ax_hermes_o_dram_trans.tick_params(axis='both')
ax_hermes_o_dram_trans.tick_params(labeltop=False)
ax_hermes_o_dram_trans.tick_params(axis='x',
                                   which='both',
                                   bottom=False,
                                   top=False)

ax_hermes_o_dram_trans.set_ylim([-100.0, 100.0])

ax_hermes_o_dram_trans.yaxis.set_major_locator(MultipleLocator(100))
ax_hermes_o_dram_trans.yaxis.set_major_formatter('{x:.0f}')
ax_hermes_o_dram_trans.yaxis.set_minor_locator(MultipleLocator(50))
ax_hermes_o_dram_trans.yaxis.set_minor_formatter('{x:.0f}')

for tick in ax_hermes_o_dram_trans.xaxis.get_major_ticks():
    tick.label1.set_horizontalalignment('left')

ax_hermes_o_dram_trans.legend(loc='upper left', edgecolor='white', fancybox=False, framealpha=0.0, ncol=2,
                              fontsize=5
                              )

# Working on the second subplot that will contain the mean for each benchmark suite.
xticklabels = mean_keys
bar_width, index = (1 - cat_spacing) / \
    len(key_list), np.arange(1, len(xticklabels) + 1)

for i, (e, c) in enumerate(zip(key_list, colors)):
    ax_hermes_o_dram_trans_mean.bar(index + i * bar_width + cat_spacing / 2,
                                    df_dram_trans_mean[e], width=bar_width, linewidth=0.2, edgecolor='black', align='edge', label=labels_dict[e], color=c)

for b, k in zip(ax_hermes_o_dram_trans_mean.patches, key_list):
    ax_hermes_o_dram_trans_mean.annotate(labels_dict[k], (b.get_x() + b.get_width() / 2, 18.5), size=3.5, rotation=90,
                                         #    ha='center',
                                         # va='center',
                                         # xytext=(0, 10), textcoords='offset points'
                                         )

ax_hermes_o_dram_trans_mean.set_xticks(index)
ax_hermes_o_dram_trans_mean.set_xticklabels([])
# ax_hermes_o_dram_trans_mean.bar_label(ax_hermes_o_dram_trans_mean.containers[1], labels=gmean_keys, label_type='edge', rotation=90, fontsize=5, padding=3)
ax_hermes_o_dram_trans_mean.set_ylim([-17.5, 60.0])
ax_hermes_o_dram_trans_mean.grid(
    color='grey', linestyle='-', linewidth=0.25)
ax_hermes_o_dram_trans_mean.grid(True, which='minor', color='grey',
                            linestyle='--', linewidth=0.2, axis='y')
ax_hermes_o_dram_trans_mean.set_axisbelow(True)
ax_hermes_o_dram_trans_mean.set_axisbelow(True)
ax_hermes_o_dram_trans_mean.tick_params(axis='y', which='minor', labelsize=7.5)

ax_hermes_o_dram_trans_mean.yaxis.set_major_locator(MultipleLocator(60))
ax_hermes_o_dram_trans_mean.yaxis.set_major_formatter('{x:.0f}')
ax_hermes_o_dram_trans_mean.yaxis.set_minor_locator(MultipleLocator(15))
ax_hermes_o_dram_trans_mean.yaxis.set_minor_formatter('{x:.0f}')

plt.savefig('plots/single_core/single_core_berti_evaluation_dram_transactions_alt.pdf',
            format='pdf', dpi='figure')
plt.savefig('plots/single_core/single_core_berti_evaluation_dram_transactions_alt.png',
            format='png', dpi='figure')


# In[ ]:


dict_l1d_accuracy = {
    'baseline_cascade_lake_spp_ppf': [s[0].sets[0]['mean']['l1d_prefetcher']['accuracy'] for s in workload_sets],
    'baseline_cascade_lake_hermes_o': [s[1].sets[0]['mean']['l1d_prefetcher']['accuracy'] for s in workload_sets],
    'baseline_cascade_lake_berti_spp_ppf_hermes_o': [s[3].sets[0]['mean']['l1d_prefetcher']['accuracy'] for s in workload_sets],
    'baseline_cascade_lake_berti_tlp_layered_core_l1d_f20_-25': [s[2].sets[0]['mean']['l1d_prefetcher']['accuracy'] for s in workload_sets],
}

df_l1d_accuracy = pandas.DataFrame(
    dict_l1d_accuracy, columns=dict_l1d_accuracy.keys(), index=['SPEC', 'GAP', 'ALL'])

df_l1d_accuracy *= 100.0

# # Sorting by geomean speed-up.
# df_l1d_accuracy.sort_values(
#     by='geomean', axis='columns', inplace=True)
# df_tmp = df_l1d_accuracy[df_l1d_accuracy.index != 'geomean'].sort_values(
#     by=df_l1d_accuracy.columns.to_list()[-1], axis='rows', inplace=False)
# df_l1d_accuracy = pandas.concat(
#     [df_tmp, df_l1d_accuracy[df_l1d_accuracy.index == 'geomean']])

display(df_l1d_accuracy)


# In[ ]:


# Here is the actual plotting material.
fig_l1d_pref_accuracy = plt.figure(
    constrained_layout=True, figsize=set_size(fig_width), dpi=500)
fig_l1d_pref_accuracy.tight_layout(pad=0)
gs = GridSpec(1, 1, figure=fig_l1d_pref_accuracy)

fig_l1d_pref_accuracy = fig_l1d_pref_accuracy.add_subplot(
    gs[:])
fig_l1d_pref_accuracy.margins(x=0, tight=True)

xticklabels = df_l1d_accuracy.index.tolist()
xticklabels = [sub_re_trailing_sdc.sub(repl='', string=e) for e in xticklabels]
xticklabels = [sub_re_trailing_und.sub(
    repl=r'\_', string=e) for e in xticklabels]

key_list = df_l1d_accuracy.columns.to_list()
# key_list = ['hermes_o_pc_based_2k_entries', 'hermes_o_pc_based', 'popet_o', 'hermes_o_perfect']

cat_spacing = 0.1
bar_width, index = (1 - cat_spacing) / \
    len(key_list), np.arange(1, len(xticklabels) + 1)

colors = cm.get_cmap(plot_cmp)(np.linspace(
    0.0, 1.0, len(key_list), endpoint=True))

for i, (e, c) in enumerate(zip(key_list, colors)):
    fig_l1d_pref_accuracy.bar(index + (i - 1) * (bar_width),
                              df_l1d_accuracy[e], width=bar_width, edgecolor='black', linewidth=0.2, align='center', label=labels_dict[e], color=c)

fig_l1d_pref_accuracy.set_xticks(index)
fig_l1d_pref_accuracy.set_xticklabels(xticklabels, rotation=0)
# fig_l1d_pref_accuracy.set_xticklabels([])
fig_l1d_pref_accuracy.grid(
    color='grey', linestyle='-', linewidth=0.25)
fig_l1d_pref_accuracy.set_axisbelow(True)

fig_l1d_pref_accuracy.set_ylabel(r'Accuracy (\%)')

fig_l1d_pref_accuracy.tick_params(axis='both')
fig_l1d_pref_accuracy.tick_params(labeltop=False)

fig_l1d_pref_accuracy.set_ylim([0, 100.0])

fig_l1d_pref_accuracy.legend(loc='upper left', edgecolor='white', fancybox=False, framealpha=0.0,
                             ncol=3,
                             fontsize=5,
                             #    labelspacing=1.0,
                             #    bbox_to_anchor=(0, 0.925, 1, 0.25),
                             #    mode='expand'
                             )

for tick in fig_l1d_pref_accuracy.xaxis.get_major_ticks():
    tick.label1.set_horizontalalignment('center')

plt.savefig('plots/single_core/single_core_berti_evaluation_l1d_prefetcher_accuracy.pdf',
            format='pdf', dpi='figure')
plt.savefig('plots/single_core/single_core_berti_evaluation_l1d_prefetcher_accuracy.png',
            format='png', dpi='figure')


# In[ ]:


dict_l1d_pref_useless_spec = {
    'l2c': [final_res_set_spec[-1].sets[0][k]['l1d_prefetcher']['useless']['l2c'] for k in spec_keys if k != 'mean'],
    'llc': [final_res_set_spec[-1].sets[0][k]['l1d_prefetcher']['useless']['llc'] for k in spec_keys if k != 'mean'],
    'dram': [final_res_set_spec[-1].sets[0][k]['l1d_prefetcher']['useless']['dram'] for k in spec_keys if k != 'mean'],
}

df_l1d_pref_useless_spec = pandas.DataFrame(
    dict_l1d_pref_useless_spec, columns=dict_l1d_pref_useless_spec.keys(), index=[k for k in spec_keys if k != 'mean'])

# # Sorting by geomean speed-up.
# df_l1d_pref_useless_spec.sort_values(
#     by='geomean', axis='columns', inplace=True)
# df_tmp = df_l1d_pref_useless_spec[df_l1d_pref_useless_spec.index != 'mean'].sort_values(
#     by=df_l1d_pref_useless_spec.columns.to_list()[-1], axis='rows', inplace=False)
# df_l1d_pref_useless_spec = pandas.concat(
#     [df_tmp, df_l1d_pref_useless_spec[df_l1d_pref_useless_spec.index == 'mean']])

# speedup_gapbs_keys = df_l1d_pref_useless_spec.index.to_list()
# gapbs_keys = speedup_gapbs_keys[:-1] + ['mean']

display(df_l1d_pref_useless_spec)

# Labels for the plots.
labels_dict = {
    'l2c': 'L2C',
    'llc': 'LLC',
    'dram': 'DRAM',
}


# In[ ]:


dict_l1d_pref_useless_gapbs = {
    'l2c': [final_res_set_gapbs[-1].sets[0][k]['l1d_prefetcher']['useless']['l2c'] for k in gapbs_keys if k != 'mean'],
    'llc': [final_res_set_gapbs[-1].sets[0][k]['l1d_prefetcher']['useless']['llc'] for k in gapbs_keys if k != 'mean'],
    'dram': [final_res_set_gapbs[-1].sets[0][k]['l1d_prefetcher']['useless']['dram'] for k in gapbs_keys if k != 'mean'],
}

df_l1d_pref_useless_gapbs = pandas.DataFrame(
    dict_l1d_pref_useless_gapbs, columns=dict_l1d_pref_useless_gapbs.keys(), index=[k for k in gapbs_keys if k != 'mean'])

display(df_l1d_pref_useless_gapbs)

# Labels for the plots.
labels_dict = {
    'l2c': 'L2C',
    'llc': 'LLC',
    'dram': 'DRAM',
}


# In[ ]:


df_l1d_pref_useless = pandas.concat(
    [df_l1d_pref_useless_spec, df_l1d_pref_useless_gapbs])

df_l1d_pref_useless_mean = pandas.DataFrame({
    'l2c': [np.mean(df_l1d_pref_useless['l2c'])],
    'llc': [np.mean(df_l1d_pref_useless['llc'])],
    'dram': [np.mean(df_l1d_pref_useless['dram'])],
}, index=['AVG'])

display(df_l1d_pref_useless)
display(df_l1d_pref_useless_mean)


# In[ ]:


markers = ['.', 's', 'o', 'x', '>', '<', 'v', '^', 'h', 'D']

# Here is the actual plotting material.
fig_hit_miss_l1d = plt.figure(
    constrained_layout=True, figsize=set_size(fig_width), dpi=500)
fig_hit_miss_l1d.tight_layout(pad=0)
gs = GridSpec(nrows=1, ncols=5, figure=fig_hit_miss_l1d)

ax_l1d_useless_loc, ax_l1d_useless_loc_mean = fig_hit_miss_l1d.add_subplot(
    gs[0, :4]), fig_hit_miss_l1d.add_subplot(gs[0, 4:])
ax_l1d_useless_loc.margins(x=0, tight=True)

xticklabels = df_l1d_pref_useless.index.tolist()
xticklabels = [sub_re_trailing_sdc.sub(repl='', string=e) for e in xticklabels]
xticklabels = [sub_re_trailing_und.sub(
    repl=r'\_', string=e) for e in xticklabels]

key_list = df_l1d_pref_useless.columns.to_list()
# key_list = ['hermes_o_pc_based', 'popet_o', 'hermes_o_perceptron_pc_pfn']

cat_spacing = 0.075
bar_width, index = (1 - cat_spacing) / \
    len(key_list), np.arange(1, len(xticklabels) + 1)

colors = cm.get_cmap(plot_cmp)(np.linspace(
    0.0, 1.0, len(key_list) + 1, endpoint=True))[1:][::-1]

prev = np.array([0.0 for _ in range(len(df_l1d_pref_useless))])
bars = None

for i, (e, c, m) in enumerate(zip(key_list, colors, markers)):
    bars = ax_l1d_useless_loc.bar(index + (cat_spacing / 2),
                                  df_l1d_pref_useless[e],
                                  bottom=prev,
                                  edgecolor='black',
                                  linewidth=0.2,
                                  align='edge',
                                  label=labels_dict[e], color=c)

    prev += np.array(df_l1d_pref_useless[e].to_list())

ax_l1d_useless_loc.axvspan(xmin=0, xmax=len(
    df_l1d_pref_useless_spec) + 1, facecolor='grey', alpha=0.25, zorder=-1)
# ax_l1d_useless_loc.axvline(x=len(df_l1d_pref_useless_spec) + len(df_l1d_pref_useless_gapbs) + 1, color='red', linestyle='--', linewidth=0.35)

# Annotating the 5th to last bar of the plot.
ax_l1d_useless_loc.annotate(f'{prev[-4]:.2f}', (bars.patches[-4].get_x() + bars.patches[-4].get_width() / 2 - 3.5, 170
                                                ), ha='center', va='center', textcoords='offset points', xytext=(0, 9), size=4)

ax_l1d_useless_loc.set_xticks(index)
# ax_l1d_useless_loc.set_xticklabels(xticklabels, rotation=90, fontsize=5)
ax_l1d_useless_loc.set_xticklabels([])
ax_l1d_useless_loc.grid(
    color='grey', linestyle='-', linewidth=0.25, axis='y')
ax_l1d_useless_loc.set_axisbelow(True)

ax_l1d_useless_loc.set_ylabel(
    'Prefetches Per Kilo\nInstructions (PPKI)', fontsize=8)

ax_l1d_useless_loc.tick_params(axis='both')
ax_l1d_useless_loc.tick_params(labeltop=False)
ax_l1d_useless_loc.tick_params(axis='x',
                               which='both',
                               bottom=False,
                               top=False)

# ax_l1d_useless_loc.set_yscale('log')
ax_l1d_useless_loc.set_ylim([0.0, 30.0])

for tick in ax_l1d_useless_loc.xaxis.get_major_ticks():
    tick.label1.set_horizontalalignment('left')

ax_l1d_useless_loc.legend(loc='upper center', edgecolor='white', fancybox=False, framealpha=0.0, ncol=3,
                          bbox_to_anchor=(0.5, 1.2),
                          fontsize=5
                          )

# Annotating the benchmark suites on the plots.
ax_l1d_useless_loc.annotate(
    'SPEC', (len(spec_keys) / 2, 25), ha='center', va='center', size=7)
ax_l1d_useless_loc.annotate('GAP', (len(
    spec_keys) + len(gapbs_keys) / 2, 25), ha='center', va='center', size=7)

# Plotting the mean in a seperate subplot.
xticklabels = df_l1d_pref_useless_mean.index.to_list()
cat_spacing = 0.075
bar_width, index = (1 - cat_spacing) / \
    len(key_list), np.arange(1, len(xticklabels) + 1)

colors = cm.get_cmap(plot_cmp)(np.linspace(
    0.0, 1.0, len(key_list) + 1, endpoint=True))[1:][::-1]
prev = np.array([0.0 for _ in range(len(df_l1d_pref_useless_mean))])

for i, (e, c, m) in enumerate(zip(key_list, colors, markers)):
    bars = ax_l1d_useless_loc_mean.bar(index + (cat_spacing / 2),
                                       df_l1d_pref_useless_mean[e],
                                       bottom=prev,
                                       edgecolor='black',
                                       linewidth=0.2,
                                       align='edge',
                                       label=labels_dict[e], color=c)

    prev += df_l1d_pref_useless_mean[e]

ax_l1d_useless_loc_mean.set_ylim([0.0, 15.0])
ax_l1d_useless_loc_mean.set_xticks(index)
ax_l1d_useless_loc_mean.set_xticklabels([])
ax_l1d_useless_loc_mean.bar_label(ax_l1d_useless_loc_mean.containers[-1], labels=[
                                  'AVG'], label_type='edge', rotation=0, fontsize=5, padding=3)
ax_l1d_useless_loc_mean.grid(
    color='grey', linestyle='-', linewidth=0.25)
ax_l1d_useless_loc_mean.set_axisbelow(True)

plt.savefig('plots/single_core/single_core_berti_l1d_pref_useless.pdf',
            format='pdf', dpi='figure')
plt.savefig('plots/single_core/single_core_berti_l1d_pref_useless.png',
            format='png', dpi='figure')


# In[ ]:


dict_l1d_pref_useful_spec = {
    'l2c': [final_res_set_spec[-1].sets[0][k]['l1d_prefetcher']['useful']['l2c'] for k in spec_keys if k != 'mean'],
    'llc': [final_res_set_spec[-1].sets[0][k]['l1d_prefetcher']['useful']['llc'] for k in spec_keys if k != 'mean'],
    'dram': [final_res_set_spec[-1].sets[0][k]['l1d_prefetcher']['useful']['dram'] for k in spec_keys if k != 'mean'],
}

df_l1d_pref_useful_spec = pandas.DataFrame(
    dict_l1d_pref_useful_spec, columns=dict_l1d_pref_useful_spec.keys(), index=[k for k in spec_keys if k != 'mean'])

display(np.mean(df_l1d_pref_useful_spec))


# In[ ]:


dict_l1d_pref_useful_gapbs = {
    'l2c': [final_res_set_gapbs[-1].sets[0][k]['l1d_prefetcher']['useful']['l2c'] for k in gapbs_keys if k != 'mean'],
    'llc': [final_res_set_gapbs[-1].sets[0][k]['l1d_prefetcher']['useful']['llc'] for k in gapbs_keys if k != 'mean'],
    'dram': [final_res_set_gapbs[-1].sets[0][k]['l1d_prefetcher']['useful']['dram'] for k in gapbs_keys if k != 'mean'],
}

df_l1d_pref_useful_gapbs = pandas.DataFrame(
    dict_l1d_pref_useful_gapbs, columns=dict_l1d_pref_useful_gapbs.keys(), index=[k for k in gapbs_keys if k != 'mean'])

display(np.mean(df_l1d_pref_useful_gapbs))


# In[ ]:


df_l1d_pref_useful = pandas.concat(
    [df_l1d_pref_useful_spec, df_l1d_pref_useful_gapbs])

df_l1d_pref_useful_mean = pandas.DataFrame({
    'l2c': [np.mean(df_l1d_pref_useful['l2c'])],
    'llc': [np.mean(df_l1d_pref_useful['llc'])],
    'dram': [np.mean(df_l1d_pref_useful['dram'])],
}, index=['AVG'])

# df_l1d_pref_useful = pandas.concat([df_l1d_pref_useful, df_l1d_pref_useful_mean])

display(df_l1d_pref_useful)
display(df_l1d_pref_useful_mean)


# In[ ]:


markers = ['.', 's', 'o', 'x', '>', '<', 'v', '^', 'h', 'D']

# Here is the actual plotting material.
fig_hit_miss_l1d = plt.figure(
    constrained_layout=True, figsize=set_size(fig_width), dpi=500)
fig_hit_miss_l1d.tight_layout(pad=0)
gs = GridSpec(nrows=1, ncols=5, figure=fig_hit_miss_l1d)

ax_l1d_useful_loc, ax_l1d_useful_loc_mean = fig_hit_miss_l1d.add_subplot(
    gs[0, :4]), fig_hit_miss_l1d.add_subplot(gs[0, 4:])
ax_l1d_useful_loc.margins(x=0, tight=True)

xticklabels = df_l1d_pref_useful.index.tolist()
xticklabels = [sub_re_trailing_sdc.sub(repl='', string=e) for e in xticklabels]
xticklabels = [sub_re_trailing_und.sub(
    repl=r'\_', string=e) for e in xticklabels]

key_list = df_l1d_pref_useful.columns.to_list()
# key_list = ['hermes_o_pc_based', 'popet_o', 'hermes_o_perceptron_pc_pfn']

cat_spacing = 0.075
bar_width, index = (1 - cat_spacing) / \
    len(key_list), np.arange(1, len(xticklabels) + 1)

colors = cm.get_cmap(plot_cmp)(np.linspace(
    0.0, 1.0, len(key_list) + 1, endpoint=True))[1:][::-1]

prev = np.array([0.0 for _ in range(len(df_l1d_pref_useful))])
bars = None

for i, (e, c, m) in enumerate(zip(key_list, colors, markers)):
    bars = ax_l1d_useful_loc.bar(index + (cat_spacing / 2),
                                  df_l1d_pref_useful[e],
                                  bottom=prev,
                                  edgecolor='black',
                                  linewidth=0.2,
                                  align='edge',
                                  label=labels_dict[e], color=c)

    prev += np.array(df_l1d_pref_useful[e].to_list())

ax_l1d_useful_loc.axvspan(xmin=0, xmax=len(
    df_l1d_pref_useful_spec) + 1, facecolor='grey', alpha=0.25, zorder=-1)
# ax_l1d_useful_loc.axvline(x=len(df_l1d_pref_useful_spec) + len(df_l1d_pref_useful_gapbs) + 1, color='red', linestyle='--', linewidth=0.35)

# Annotating the 5th to last bar of the plot.
ax_l1d_useful_loc.annotate(f'{prev[-4]:.2f}', (bars.patches[-4].get_x() + bars.patches[-4].get_width() / 2 - 3.5, 160
                                                ), ha='center', va='center', textcoords='offset points', xytext=(0, 9), size=4)

ax_l1d_useful_loc.set_xticks(index)
# ax_l1d_useful_loc.set_xticklabels(xticklabels, rotation=90, fontsize=5)
ax_l1d_useful_loc.set_xticklabels([])
ax_l1d_useful_loc.grid(
    color='grey', linestyle='-', linewidth=0.25, axis='y')
ax_l1d_useful_loc.set_axisbelow(True)

ax_l1d_useful_loc.set_ylabel(
    'Prefetches Per Kilo\nInstructions (PPKI)', fontsize=8)

ax_l1d_useful_loc.tick_params(axis='both')
ax_l1d_useful_loc.tick_params(labeltop=False)
ax_l1d_useful_loc.tick_params(axis='x',
                               which='both',
                               bottom=False,
                               top=False)

# ax_l1d_useful_loc.set_yscale('log')
ax_l1d_useful_loc.set_ylim([0.0, 15.0])

for tick in ax_l1d_useful_loc.xaxis.get_major_ticks():
    tick.label1.set_horizontalalignment('left')

ax_l1d_useful_loc.legend(loc='upper center', edgecolor='white', fancybox=False, framealpha=0.0, ncol=3,
                          bbox_to_anchor=(0.5, 1.2),
                          fontsize=5
                          )

# Annotating the benchmark suites on the plots.
ax_l1d_useful_loc.annotate(
    'SPEC', (len(spec_keys) / 2, 125), ha='center', va='center', size=7)
ax_l1d_useful_loc.annotate('GAP', (len(
    spec_keys) + len(gapbs_keys) / 2, 125), ha='center', va='center', size=7)

# Plotting the mean in a seperate subplot.
xticklabels = df_l1d_pref_useful_mean.index.to_list()
cat_spacing = 0.075
bar_width, index = (1 - cat_spacing) / \
    len(key_list), np.arange(1, len(xticklabels) + 1)

colors = cm.get_cmap(plot_cmp)(np.linspace(
    0.0, 1.0, len(key_list) + 1, endpoint=True))[1:][::-1]
prev = np.array([0.0 for _ in range(len(df_l1d_pref_useful_mean))])

for i, (e, c, m) in enumerate(zip(key_list, colors, markers)):
    bars = ax_l1d_useful_loc_mean.bar(index + (cat_spacing / 2),
                                       df_l1d_pref_useful_mean[e],
                                       bottom=prev,
                                       edgecolor='black',
                                       linewidth=0.2,
                                       align='edge',
                                       label=labels_dict[e], color=c)

    prev += df_l1d_pref_useful_mean[e]

ax_l1d_useful_loc_mean.yaxis.set_major_locator(MultipleLocator(2.5))
ax_l1d_useful_loc_mean.yaxis.set_major_formatter('{x:.1f}')
# ax_l1d_useful_loc_mean.yaxis.set_minor_locator(MultipleLocator(15))
# ax_l1d_useful_loc_mean.yaxis.set_minor_formatter('{x:.1f}')

ax_l1d_useful_loc_mean.set_ylim([0.0, 2.5])
ax_l1d_useful_loc_mean.set_xticks(index)
ax_l1d_useful_loc_mean.set_xticklabels([])
ax_l1d_useful_loc_mean.bar_label(ax_l1d_useful_loc_mean.containers[-1], labels=[
                                  'AVG'], label_type='edge', rotation=0, fontsize=5, padding=3)
ax_l1d_useful_loc_mean.grid(
    color='grey', linestyle='-', linewidth=0.25)
ax_l1d_useful_loc_mean.grid(True, which='minor', color='grey',
                             linestyle='--', linewidth=0.2, axis='y')
ax_l1d_useful_loc_mean.set_axisbelow(True)

plt.savefig('plots/single_core/single_core_berti_l1d_pref_useful.pdf',
            format='pdf', dpi='figure')
plt.savefig('plots/single_core/single_core_berti_l1d_pref_useful.png',
            format='png', dpi='figure')

