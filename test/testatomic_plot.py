import matplotlib.pyplot as plt
import numpy as np

def plot_hist(filename):
    data = {}
    for i in range(3):
        data[i] = np.array([], dtype='i')

    with open(filename) as f:
        for line in f:
            values = line.split()
            series = int(values[0])
            data[series] = np.append(data[series], int(values[1]))

    titles = [
        "SRW Locks reentrant (this patch)",
        "CriticalSections",
        "SRW Locks non reentrant",
    ]
    bins=np.array(range(0, int(max(data[0].max(), data[1].max(), data[2].max()) * 1.05 + 1)))
    plt.figure(figsize=(8,4))
    for j, label in enumerate(plt.axes(xticks=bins[::10]).xaxis.get_ticklabels()):
        every_n = 10
        if j % every_n != 0:
            label.set_visible(False)
    bins = bins - 0.5
    for i in range(3):
        plt.hist(data[i], bins=bins, label=titles[i], alpha= (0.5 if i > 1 else 1))
    plt.legend()
    plt.xlabel("ms")
    plt.show()

if __name__ == '__main__':
    plot_hist('testatomic.csv')
