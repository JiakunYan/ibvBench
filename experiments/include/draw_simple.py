#!/usr/bin/env python3

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.ticker import ScalarFormatter, FormatStrFormatter
import pandas as pd
import sys,os
import json

def line_plot(title, xlabel, ylabel, data, fname='out.pdf', add_perfect=True, is_show=False, is_save=False):
    fig, ax = plt.subplots()

    domain = set()
    lrange = set()

    for datum in data:
        for x in datum['domain']:
            domain.add(x)
        for y in datum['range']:
            lrange.add(y)

    domain = sorted(domain)

    for datum in data:
        marker='.'
        linestyle=None
        color=None
        if 'marker' in datum:
            marker = datum['marker']
        if 'linestyle' in datum:
            # print('Setting linestyle for %s to %s' % (datum['label'], datum['linestyle']))
            linestyle = datum['linestyle']
        if 'color' in datum:
            color = datum['color']
        if 'error' in datum:
            ax.errorbar(datum['domain'], datum['range'], datum['error'], label=datum['label'], marker=marker, linestyle=linestyle, color=color, markerfacecolor='white', capsize=3)
        else:
            ax.plot(datum['domain'], datum['range'], label=datum['label'], marker=marker, linestyle=linestyle, color=color, markerfacecolor='white')

    if add_perfect is True:
        perfect_range = datum['range'][0] / (np.array(domain) / domain[0])
        for y in perfect_range:
            lrange.add(y)
        ax.loglog(domain, perfect_range, label='perfect scaling', linestyle='--', color='grey')

    ymin = min(lrange)
    ymax = max(lrange)

    ytick_range = list([2**x for x in range(-5, 40)])
    yticks = []
    for tick in ytick_range:
        if tick >= ymin and tick <= ymax:
            yticks.append(tick)
    if len(yticks) > 0: yticks.append(yticks[-1]*2)
    if len(yticks) > 0: yticks.append(yticks[0]/2)

    ax.minorticks_off()
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xticks(domain)
    ax.set_xticklabels(list(map(lambda x: str(x), domain)))
    if len(yticks) > 0: ax.set_yticks(yticks)
    ax.yaxis.set_major_formatter(FormatStrFormatter('%.2f'))

    plt.title(title)
    plt.xlabel(xlabel)
    plt.ylabel(ylabel)
    plt.legend(loc='best')
    plt.tight_layout()
    if is_show:
        plt.show()
    if is_save:
        plt.savefig(fname)

def draw_simple(config):
    df = pd.read_csv(config["input"])
    lines = []
    x_key = config["x_key"]
    y_key = config["y_key"]
    title = config["name"]

    current_domain = []
    current_value = []
    current_error = []
    for x in df[x_key].unique():
        y = df[df[x_key] == x].median()[y_key]
        error = df[df[x_key] == x].std()[y_key]
        if y is np.nan:
            continue
        if y == 0:
            continue
        current_domain.append(float(x))
        current_value.append(float(y))
        current_error.append(float(error))
    lines.append({'label': "", 'domain': current_domain, 'range': current_value, 'error': current_error})

    with open(os.path.join(config["output"], "{}.json".format(config["name"])), 'w') as outfile:
        json.dump(lines, outfile)
    line_plot(title, x_key, y_key, lines, os.path.join(config["output"], "{}.png".format(config["name"])), False, is_show=False, is_save=True)

def draw_tag(config):
    df = pd.read_csv(config["input"])
    lines = []
    x_key = config["x_key"]
    y_key = config["y_key"]
    tag_key = config["tag_key"]
    title = "{}_{}".format(config["name"], config["tag_key"])

    for tag in df[tag_key].unique():
        criterion = (df[tag_key] == tag)
        df1 = df[criterion]
        current_domain = []
        current_value = []
        current_error = []
        for x in df1[x_key].unique():
            y = df1[df1[x_key] == x].median()[y_key]
            error = df1[df1[x_key] == x].std()[y_key]
            if y is np.nan:
                continue
            if y == 0:
                continue
            current_domain.append(float(x))
            current_value.append(float(y))
            current_error.append(float(error))
        lines.append({'label': str(tag), 'domain': current_domain, 'range': current_value, 'error': current_error})

    if len(lines) == 0:
        print("Error! Got 0 line!")
        exit(1)
    with open(os.path.join(config["output"], "{}.json".format(config["name"])), 'w') as outfile:
        json.dump(lines, outfile)
    line_plot(title, x_key, y_key, lines, os.path.join(config["output"], "{}.png".format(config["name"])), False, is_show=False, is_save=True)

def draw_tags(config, df = None):
    if df is None:
        df = pd.read_csv(config["input"])
    lines = []
    x_key = config["x_key"]
    y_key = config["y_key"]
    tag_keys = config["tag_keys"]
    title = "{}_{}".format(config["name"], config["tag_keys"])

    for index in df[tag_keys].drop_duplicates().index:
        criterion = True
        tags = []
        for tag_key in tag_keys:
            criterion = (df[tag_key] == df[tag_key][index]) & criterion
            tags.append(df[tag_key][index])
        df1 = df[criterion]
        current_domain = []
        current_value = []
        current_error = []
        for x in df1[x_key].unique():
            y = df1[df1[x_key] == x].median()[y_key]
            error = df1[df1[x_key] == x].std()[y_key]
            if y is np.nan:
                continue
            if y == 0:
                continue
            current_domain.append(float(x))
            current_value.append(float(y))
            current_error.append(float(error))
        lines.append({'label': str(tags), 'domain': current_domain, 'range': current_value, 'error': current_error})

    if len(lines) == 0:
        print("Error! Got 0 line!")
        exit(1)
    with open(os.path.join(config["output"], "{}.json".format(config["name"])), 'w') as outfile:
        json.dump(lines, outfile)
    line_plot(title, x_key, y_key, lines, os.path.join(config["output"], "{}.png".format(config["name"])), False, is_show=False, is_save=True)