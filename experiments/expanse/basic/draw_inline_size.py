import re
import glob
import numpy as np
import ast
import pandas as pd
import os,sys
sys.path.append("../../include")
from draw_simple import *

name = "inline_size"
input_path = "draw/"
all_labels = ["task", "Size(B)", "latency(us)", "throughput(Mmsg/s)", "bandwidth(MB/s)"]

def plot(df, x_key, y_key, tag_key, title):
    fig, ax = plt.subplots()
    lines = parse_tag(df, x_key, y_key, tag_key)
    for line in lines:
        ax.errorbar(line["x"], line["y"], line["error"], label=line['label'], marker='.', markerfacecolor='white', capsize=3)
    ax.set_xlabel(x_key)
    ax.set_ylabel(y_key)
    ax.set_title(title)
    ax.legend()
    output_png_name = os.path.join("draw", "{}.png".format(title))
    fig.savefig(output_png_name)

def batch(df):
    df1 = df[df.apply(lambda row: "-t 1" in row["task"] and
                                  row["Size(B)"] < 10, axis=1)]
    plot(df1, "Size(B)", "latency(us)", "task", "latency (inline size)")

if __name__ == "__main__":
    df = pd.read_csv(os.path.join(input_path, name + ".csv"))
    batch(df)