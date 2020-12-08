import matplotlib.pyplot as plt
import numpy as np

def plot_hist(filename):
    data = {}
    for i in range(6):
        data[i] = np.array([], dtype='i')

    with open(filename) as f:
        for line in f:
            values = line.split()
            kernel = int(bool(int(values[0])))
            data[0 + kernel] = np.append(data[0 + kernel], int(values[1]))
            data[2 + kernel] = np.append(data[2 + kernel], int(values[2]))
            data[4 + kernel] = np.append(data[4 + kernel], int(values[3]))
    
    titles = [
        "Uncontended",
        "Contended WaitTimeout",
        "Contended TryWait",
    ]
    for i in range(3):
        bins=np.array(range(0, int(max(data[i*2 + 0].max(), data[i*2 + 1].max()) * 1.05 + 1)))
        plt.figure(figsize=(8,4))
        for j, label in enumerate(plt.axes(xticks=bins).xaxis.get_ticklabels()):
            every_n = int(len(bins) / 20 + 0.5)
            if j % every_n != 0:
                label.set_visible(False)
        bins = bins - 0.5
        plt.hist(data[i*2 + 1], bins=bins)
        plt.hist(data[i*2 + 0], bins=bins)
        plt.yscale("log")
        plt.xlabel("ms")
        plt.title(titles[i])
        plt.show()

if __name__ == '__main__':
    plot_hist('testsem.csv')