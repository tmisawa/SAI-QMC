#!/usr/bin/env python3
from __future__ import annotations

import csv
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


ROOT = Path(__file__).resolve().parent
FULLDIAG_REFERENCE = ROOT / "reference"
U = 4.0
MU = U / 2.0
SIZES = [4, 6, 8]


def read_params(path: Path) -> dict[str, str]:
    params: dict[str, str] = {}
    for raw in path.read_text().splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line or "=" not in line:
            continue
        key, val = line.split("=", 1)
        params[key.strip()] = val.strip()
    return params


def load_qmc(path: Path) -> np.ndarray:
    rows = []
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        rows.append([float(x) for x in line.split()])
    return np.asarray(rows, dtype=float)


def load_fulldiag(L: int) -> np.ndarray:
    data = np.loadtxt(FULLDIAG_REFERENCE / f"L{L}_U4_FullDiag.dat")
    order = np.argsort(data[:, 0])
    return data[order]


def interp(x: np.ndarray, y: np.ndarray, xq: np.ndarray) -> np.ndarray:
    return np.interp(xq, x, y)


def main() -> None:
    all_rows: list[dict[str, object]] = []
    ed_curves: dict[int, np.ndarray] = {}
    qmc_curves: dict[int, np.ndarray] = {}

    for L in SIZES:
        params = read_params(ROOT / f"input_L{L}.in")
        qmc = load_qmc(ROOT / f"out_L{L}.dat")
        fd = load_fulldiag(L)
        ed_curves[L] = fd
        qmc_curves[L] = qmc

        T = qmc[:, 0]
        beta = 1.0 / T
        qmc_ehub = qmc[:, 1]
        qmc_dehub = qmc[:, 2]
        qmc_egc = qmc[:, 3]
        qmc_degc = qmc[:, 4]
        qmc_n = qmc[:, 7]
        qmc_d = qmc[:, 9]
        qmc_dd = qmc[:, 10]
        sign = qmc[:, 11]

        ed_T = fd[:, 0]
        ed_egc = interp(ed_T, fd[:, 1], T)
        ed_n = interp(ed_T, fd[:, 3], T)
        ed_d_total = interp(ed_T, fd[:, 6], T)
        ed_ehub = ed_egc + MU * ed_n
        ed_d = ed_d_total / float(L)

        for i in range(len(T)):
            all_rows.append(
                {
                    "L": L,
                    "U": U,
                    "mu": MU,
                    "dtau": params["dtau"],
                    "nrep": params["nrep"],
                    "nwarm": params["nwarm"],
                    "nmeas": params["nmeas"],
                    "nbin": params["nbin"],
                    "beta": f"{beta[i]:.12g}",
                    "T": f"{T[i]:.12g}",
                    "qmc_Ehub": f"{qmc_ehub[i]:.12g}",
                    "qmc_dEhub": f"{qmc_dehub[i]:.12g}",
                    "ed_Ehub": f"{ed_ehub[i]:.12g}",
                    "diff_Ehub": f"{qmc_ehub[i] - ed_ehub[i]:.12g}",
                    "qmc_Ehub_per_site": f"{qmc_ehub[i] / L:.12g}",
                    "qmc_dEhub_per_site": f"{qmc_dehub[i] / L:.12g}",
                    "ed_Ehub_per_site": f"{ed_ehub[i] / L:.12g}",
                    "diff_Ehub_per_site": f"{(qmc_ehub[i] - ed_ehub[i]) / L:.12g}",
                    "qmc_Egc": f"{qmc_egc[i]:.12g}",
                    "qmc_dEgc": f"{qmc_degc[i]:.12g}",
                    "ed_Egc": f"{ed_egc[i]:.12g}",
                    "diff_Egc": f"{qmc_egc[i] - ed_egc[i]:.12g}",
                    "qmc_N": f"{qmc_n[i]:.12g}",
                    "ed_N": f"{ed_n[i]:.12g}",
                    "qmc_doublon": f"{qmc_d[i]:.12g}",
                    "qmc_dDoublon": f"{qmc_dd[i]:.12g}",
                    "ed_doublon": f"{ed_d[i]:.12g}",
                    "diff_doublon": f"{qmc_d[i] - ed_d[i]:.12g}",
                    "sign": f"{sign[i]:.12g}",
                }
            )

    fieldnames = list(all_rows[0].keys())
    with (ROOT / "comparison_qmc_vs_fulldiag.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(all_rows)
    with (ROOT / "comparison_qmc_vs_fulldiag.tsv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames, delimiter="\t")
        writer.writeheader()
        writer.writerows(all_rows)

    colors = {4: "tab:blue", 6: "tab:orange", 8: "tab:green"}
    fig, axes = plt.subplots(1, 2, figsize=(11.0, 4.5), constrained_layout=True)
    for L in SIZES:
        fd = ed_curves[L]
        qmc = qmc_curves[L]
        mask = (fd[:, 0] >= 0.09) & (fd[:, 0] <= 2.2)
        ed_ehub = fd[:, 1] + MU * fd[:, 3]
        axes[0].plot(fd[mask, 0], ed_ehub[mask] / L, color=colors[L], lw=1.8, label=f"ED L={L}")
        axes[0].errorbar(qmc[:, 0], qmc[:, 1] / L, yerr=qmc[:, 2] / L, fmt="o",
                         ms=3.5, capsize=2, color=colors[L], label=f"DQMC L={L}")
        axes[1].plot(fd[mask, 0], fd[mask, 6] / L, color=colors[L], lw=1.8, label=f"ED L={L}")
        axes[1].errorbar(qmc[:, 0], qmc[:, 9], yerr=qmc[:, 10], fmt="o",
                         ms=3.5, capsize=2, color=colors[L], label=f"DQMC L={L}")

    axes[0].set_xlabel("T")
    axes[0].set_ylabel("E_hub / L")
    axes[1].set_xlabel("T")
    axes[1].set_ylabel("doublon")
    for ax in axes:
        ax.set_xscale("log")
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize=8, ncols=2)
    fig.savefig(ROOT / "qmc_vs_fulldiag_energy_doublon.png", dpi=180)
    plt.close(fig)

    rows_by_l = {L: [r for r in all_rows if int(r["L"]) == L] for L in SIZES}
    fig, axes = plt.subplots(1, 2, figsize=(11.0, 4.5), constrained_layout=True)
    for L in SIZES:
        rows = rows_by_l[L]
        T = np.asarray([float(r["T"]) for r in rows])
        de = np.asarray([float(r["diff_Ehub_per_site"]) for r in rows])
        dde = np.asarray([float(r["qmc_dEhub_per_site"]) for r in rows])
        dd = np.asarray([float(r["diff_doublon"]) for r in rows])
        ddd = np.asarray([float(r["qmc_dDoublon"]) for r in rows])
        axes[0].errorbar(T, de, yerr=dde, fmt="o-", ms=3.5, capsize=2,
                         color=colors[L], label=f"L={L}")
        axes[1].errorbar(T, dd, yerr=ddd, fmt="o-", ms=3.5, capsize=2,
                         color=colors[L], label=f"L={L}")
    axes[0].axhline(0.0, color="black", lw=1.0)
    axes[1].axhline(0.0, color="black", lw=1.0)
    axes[0].set_xlabel("T")
    axes[0].set_ylabel("DQMC - ED: E_hub / L")
    axes[1].set_xlabel("T")
    axes[1].set_ylabel("DQMC - ED: doublon")
    for ax in axes:
        ax.set_xscale("log")
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize=8)
    fig.savefig(ROOT / "qmc_vs_fulldiag_residuals.png", dpi=180)
    plt.close(fig)


if __name__ == "__main__":
    main()
