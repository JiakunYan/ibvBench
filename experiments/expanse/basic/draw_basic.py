import pandas as pd
import os,sys
from matplotlib import pyplot as plt
sys.path.append("../../include")
from draw_simple import *

name = "basic"
input_path = "draw/"
all_labels = ["task", "Size(B)", "latency(us)", "throughput(Mmsg/s)", "bandwidth(MB/s)"]

def interactive(df):
    tasks = [
             'mpi_pingpong -t 1',
             'mpi_pingpong -t 1 UCX_TLS=rc_v',
             'ibv_pingpong_sendrecv -t 1',
             'ibv_pingpong_write -t 1',
             'ibv_pingpong_write_imm -t 1',
             # 'ibv_pingpong_read -t 1',
             'ibv_pingpong_rdv_write -t 1',
             'ibv_pingpong_rdv_write_imm -t 1',
             'ibv_pingpong_rdv_read -t 1',
             # 'mpi_pingpong -t 0',
             # 'mpi_pingpong -t 0 UCX_TLS=rc_v',
             # 'ibv_pingpong_sendrecv -t 0',
             # 'ibv_pingpong_write -t 0',
             # 'ibv_pingpong_write_imm -t 0',
             # 'ibv_pingpong_read -t 0',
             # 'ibv_pingpong_rdv_write -t 0',
             # 'ibv_pingpong_rdv_write_imm -t 0',
             # 'ibv_pingpong_rdv_read -t 0',
             ]

    df1 = df[df.apply(lambda row:
                         row["task"] in tasks and
                         (row["Size(B)"] >= 10) and
                      True,
                      axis=1)]
    x_key = "Size(B)"
    y_key = "bandwidth(MB/s)"
    tag_key = "task"
    lines = parse_tag(df1, x_key, y_key, tag_key)
    for line in lines:
        print(line)
        plt.errorbar(line["x"], line["y"], line["error"], label=line['label'], marker='.', markerfacecolor='white', capsize=3)
    plt.xlabel(x_key)
    plt.ylabel(y_key)
    plt.legend()
    plt.show()

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
    df1 = df[df.apply(lambda row:
                      "-t 0" in row["task"] and
                      "ibv_pingpong_read" not in row["task"] and
                      (row["Size(B)"] >= 10) and
                      True,
                      axis=1)]
    plot(df1, "Size(B)", "bandwidth(MB/s)", "task", "bandwidth")

    df1 = df[df.apply(lambda row:
                      "-t 1" in row["task"] and
                      "ibv_pingpong_read" not in row["task"] and
                      (row["Size(B)"] >= 10) and
                      True,
                      axis=1)]
    plot(df1, "Size(B)", "bandwidth(MB/s)", "task", "bandwidth (touch data)")

    df1 = df[df.apply(lambda row:
                      "-t 0" in row["task"] and
                      "ibv_pingpong_read" not in row["task"] and
                      "rdv" not in row["task"] and
                      (row["Size(B)"] < 10) and
                      True,
                      axis=1)]
    plot(df1, "Size(B)", "latency(us)", "task", "latency")

    df1 = df[df.apply(lambda row:
                      "-t 1" in row["task"] and
                      "ibv_pingpong_read" not in row["task"] and
                      "rdv" not in row["task"] and
                      (row["Size(B)"] < 10) and
                      True,
                      axis=1)]
    plot(df1, "Size(B)", "latency(us)", "task", "latency (touch data)")

if __name__ == "__main__":
    df = pd.read_csv(os.path.join(input_path, name + ".csv"))
    batch(df)
