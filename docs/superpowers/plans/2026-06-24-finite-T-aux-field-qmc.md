---
date: 2026-06-24
datetime: 2026-06-25 11:06 JST
model: Codex (GPT-5)
status: plan
topic: 有限温度 補助場QMC (DQMC/BSS) の TDD 実装計画
summary: |
  半充填ハバード模型の有限温度 補助場 QMC を C で実装する TDD バイトサイズ計画 (Task 0→15)。
  linalg/UDV → lattice/model → field → green(init/update/wrap) → measure → dqmc → io/main
  → 統合検証 → ED/TPQ 照合手順 → 任意格子入力 → B_l テスト。Codex 第7回レビュー反映済み
  (非可換 Green 積テスト、入力 parser 厳格化、UDV stress test、2D 検証記述修正ほか)。
---

# 有限温度 補助場QMC (DQMC/BSS) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 半充填ハバード模型の有限温度補助場QMC (BSS法) を C で実装し、小サイズで E(T) を外部 ED/TPQ と比較検証できるようにする。

**Architecture:** 格子を一般ホッピング行列 `t_ij` に抽象化し、QMCコアは `exp(±Δτ K_σ)` 行列としてのみ格子を知る。等時刻グリーン関数を起点に、Ising補助場のrank-1更新 (A.107)・wrapping (A.108)・UDV安定化で BSS スイープを回す。半充填 (μ=U/2) で符号問題なし。

**Tech Stack:** C11, LAPACK/BLAS (Fortran インタフェース; macOS=Accelerate, Linux=OpenBLAS+LAPACK), GNU Make。

**Reference:** 大塚雄一 博士論文 付録A（`../specs/2026-06-24-finite-T-aux-field-qmc-design.md` および `references/…[博士論文_200203].md`）。

---

## 規約（全タスク共通）

- **作業ディレクトリ**: すべてのコマンド（`make`, `./tests/...`, `git add/commit`）は **AF_QMC リポジトリルート**で実行する。パスはリポジトリルートからの相対（`src/...`, `tests/...`, `input/...`）。
- **行列格納**: すべて `double*` の1次元配列、**column-major**（Fortran順）。`n×n` 行列 `A` の `(i,j)` 要素は `A[i + j*n]`。LAPACK/BLAS をラッパなしで直接呼ぶための統一規約。
- **LAPACK/BLAS 呼び出し**: Fortran シンボル（末尾 `_`）を `extern` 宣言して直接呼ぶ。`dgemm_`, `dgetrf_`, `dgetri_`, `dsyev_`, `dgeqrf_`, `dorgqr_`, `dtrtri_`, `dtrsm_`。これらは Accelerate / OpenBLAS+LAPACK 双方で利用可能。
- **グリーン関数規約**: `g_ij = ⟨c_i c_j†⟩`、密度 `⟨c_i† c_j⟩ = δ_ij − g_ji`（付録A.56）。
- **スピン符号**: `σ = +1`(↑) / `σ = −1`(↓)。
- **テスト**: 各テストは独立した `tests/test_*.c`（自前 `main`）。`make test` で全ビルド＆実行。許容誤差は明示。
- **コミット**: 各タスク末尾で commit。コミット末尾に
  `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>` を付す。

---

## ファイル構成

```
AF_QMC/
  Makefile
  src/
    linalg.h  linalg.c    LAPACK/BLAS ラッパ: matmul, inverse, logdet, expm_sym, UDV安定化
    lattice.h lattice.c   格子→ホッピング行列 t_ij、二部格子符号
    model.h   model.c     K_σ 構築 (A.9)、expK/expK_inv 前計算
    field.h   field.c     補助場 s_il、λ (A.11)、N_imσ (A.93)
    green.h   green.c      Bσ行列、Green初期化 (A.95)、rank-1更新 (A.107)、wrapping (A.108)
    dqmc.h    dqmc.c       BSSスイープ、Metropolis (A.99)、符号追跡、安定化スケジュール
    measure.h measure.c    E(T)測定 (A.56 + U⟨n↑n↓⟩)、二重占有、ジャックナイフ
    rng.h     rng.c        再現可能乱数 (xoshiro256**)
    io.h      io.c         入力パース、結果出力
    main.c                 ドライバ
  tests/
    test_util.h            CHECK / CHECK_CLOSE マクロ
    test_linalg.c  test_udv.c  test_lattice.c  test_model.c
    test_field.c   test_green_init.c  test_green_update.c  test_green_wrap.c
    test_measure.c
  input/
    1d_L4_U0.txt  1d_L4_U4.txt
```

---

## Task 0: プロジェクト雛形とビルド・LAPACKリンク確認

**Files:**
- Create: `Makefile`
- Create: `tests/test_util.h`
- Create: `tests/test_link.c`

- [ ] **Step 1: テストハーネスを書く**

Create `tests/test_util.h`:

```c
#ifndef TEST_UTIL_H
#define TEST_UTIL_H
#include <stdio.h>
#include <math.h>
static int g_fail = 0;
#define CHECK(cond) do{ if(!(cond)){ printf("FAIL %s:%d  %s\n",__FILE__,__LINE__,#cond); g_fail++;} }while(0)
#define CHECK_CLOSE(a,b,tol) do{ double _a=(a),_b=(b),_d=fabs(_a-_b); \
  if(_d>(tol)){ printf("FAIL %s:%d  |%.12g - %.12g| = %.3g > %.3g\n",__FILE__,__LINE__,_a,_b,_d,(double)(tol)); g_fail++;} }while(0)
#define TEST_END() do{ if(g_fail){ printf("%d CHECK(S) FAILED\n",g_fail); return 1;} printf("OK\n"); return 0; }while(0)
#endif
```

- [ ] **Step 2: LAPACKリンク確認テストを書く（失敗する）**

Create `tests/test_link.c` — `dsyev_` で 2×2 対称行列の固有値を求め、既知値と比較:

```c
#include "test_util.h"
extern void dsyev_(const char*, const char*, const int*, double*, const int*,
                   double*, double*, const int*, int*);
int main(void){
    /* A = [[2,1],[1,2]] (column-major), eigenvalues 1 and 3 */
    int n=2, lwork=64, info;
    double A[4]={2,1,1,2}, w[2], work[64];
    dsyev_("V","U",&n,A,&n,w,work,&lwork,&info);
    CHECK(info==0);
    CHECK_CLOSE(w[0],1.0,1e-12);
    CHECK_CLOSE(w[1],3.0,1e-12);
    TEST_END();
}
```

- [ ] **Step 3: Makefile を書く**

Create `Makefile`:

```make
CC      = cc
CFLAGS  = -std=c11 -O2 -Wall -Wextra -Isrc
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  LDLIBS = -framework Accelerate
else
  LDLIBS = -llapack -lblas -lm
endif

SRC  = $(wildcard src/*.c)
OBJ  = $(SRC:.c=.o)
TESTS = $(wildcard tests/test_*.c)
TESTBIN = $(TESTS:.c=)

dqmc: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

# 各テストは src の全 .c（main.c 除く）とリンク
LIBSRC = $(filter-out src/main.c,$(SRC))
tests/test_%: tests/test_%.c $(LIBSRC)
	$(CC) $(CFLAGS) -o $@ $< $(LIBSRC) $(LDLIBS)

test: $(TESTBIN)
	@fail=0; for t in $(TESTBIN); do printf "%-28s " $$t; ./$$t || fail=1; done; \
	  if [ $$fail -ne 0 ]; then echo "SOME TESTS FAILED"; exit 1; fi; echo "ALL TESTS PASSED"

clean:
	rm -f src/*.o dqmc $(TESTBIN)
.PHONY: test clean
```

> 注: `test_link.c` は src を必要としないが上記ルールでも `$(LIBSRC)` が空のうちは問題なくリンクできる。後続タスクで src が増えても同ルールで通る。

- [ ] **Step 4: テストが通ることを確認**

Run: `make tests/test_link && ./tests/test_link`
Expected: `OK`

- [ ] **Step 5: Commit**

```bash
git add Makefile tests/test_util.h tests/test_link.c
git commit -m "chore: project scaffold + LAPACK link test"
```

---

## Task 1: linalg — 行列積・逆行列・行列式・対称行列指数

**Files:**
- Create: `src/linalg.h`, `src/linalg.c`
- Test: `tests/test_linalg.c`

- [ ] **Step 1: ヘッダを書く**

Create `src/linalg.h`:

```c
#ifndef LINALG_H
#define LINALG_H
/* すべて column-major、n×n 正方行列。配列は呼び出し側で確保。 */

/* C = A * B */
void la_matmul(int n, const double *A, const double *B, double *C);
/* C = alpha*op(A)*op(B) + beta*C ; ta/tb: 0=N,1=T */
void la_gemm(int n, int ta, int tb, double alpha, const double *A,
             const double *B, double beta, double *C);
/* Ainv = A^{-1}. 返り値 0=成功, 非0=特異 */
int  la_inverse(int n, const double *A, double *Ainv);
/* det(A) を sign * exp(logabs) で返す。A は破壊される。返り値 0=成功 */
int  la_logdet(int n, double *A, int *sign, double *logabs);
/* expA = exp(scale * A) for symmetric A（dsyev 経由）。A は破壊されない。 */
void la_expm_sym(int n, const double *A, double scale, double *expA);
/* I を単位行列に */
void la_eye(int n, double *A);
#endif
```

- [ ] **Step 2: 失敗するテストを書く**

Create `tests/test_linalg.c`:

```c
#include "test_util.h"
#include "linalg.h"
int main(void){
    int n=2;
    /* A=[[1,2],[3,4]] column-major: a[0]=1,a[1]=3,a[2]=2,a[3]=4 */
    double A[4]={1,3,2,4}, B[4]={1,0,0,1}, C[4];
    la_matmul(n,A,B,C);                      /* A*I = A */
    CHECK_CLOSE(C[0],1,1e-12); CHECK_CLOSE(C[1],3,1e-12);
    CHECK_CLOSE(C[2],2,1e-12); CHECK_CLOSE(C[3],4,1e-12);

    double Ainv[4]; int rc=la_inverse(n,A,Ainv);
    CHECK(rc==0);
    /* A*Ainv = I */
    double P[4]; la_matmul(n,A,Ainv,P);
    CHECK_CLOSE(P[0],1,1e-12); CHECK_CLOSE(P[1],0,1e-12);
    CHECK_CLOSE(P[2],0,1e-12); CHECK_CLOSE(P[3],1,1e-12);

    double A2[4]={1,3,2,4}; int sgn; double lad;
    la_logdet(n,A2,&sgn,&lad);               /* det=-2 */
    CHECK(sgn==-1); CHECK_CLOSE(exp(lad),2.0,1e-12);

    /* symmetric S=[[2,1],[1,2]], exp(1*S): eigenvalues e^1,e^3, eigvecs (1,∓1)/√2 */
    double S[4]={2,1,1,2}, E[4];
    la_expm_sym(n,S,1.0,E);
    double a=(exp(3)+exp(1))/2, b=(exp(3)-exp(1))/2;
    CHECK_CLOSE(E[0],a,1e-9); CHECK_CLOSE(E[1],b,1e-9);
    CHECK_CLOSE(E[2],b,1e-9); CHECK_CLOSE(E[3],a,1e-9);
    TEST_END();
}
```

- [ ] **Step 3: 実装を書く**

Create `src/linalg.c`:

```c
#include "linalg.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

extern void dgemm_(const char*,const char*,const int*,const int*,const int*,
                   const double*,const double*,const int*,const double*,const int*,
                   const double*,double*,const int*);
extern void dgetrf_(const int*,const int*,double*,const int*,int*,int*);
extern void dgetri_(const int*,double*,const int*,const int*,double*,const int*,int*);
extern void dsyev_(const char*,const char*,const int*,double*,const int*,
                   double*,double*,const int*,int*);

void la_eye(int n, double *A){
    memset(A,0,sizeof(double)*n*n);
    for(int i=0;i<n;i++) A[i+i*n]=1.0;
}

void la_gemm(int n,int ta,int tb,double alpha,const double *A,
             const double *B,double beta,double *C){
    dgemm_(ta?"T":"N", tb?"T":"N", &n,&n,&n,&alpha,A,&n,B,&n,&beta,C,&n);
}

void la_matmul(int n,const double *A,const double *B,double *C){
    la_gemm(n,0,0,1.0,A,B,0.0,C);
}

int la_inverse(int n,const double *A,double *Ainv){
    memcpy(Ainv,A,sizeof(double)*n*n);
    int *ipiv=malloc(sizeof(int)*n); int info;
    dgetrf_(&n,&n,Ainv,&n,ipiv,&info);
    if(info!=0){ free(ipiv); return info; }
    int lwork=n*64; double *work=malloc(sizeof(double)*lwork);
    dgetri_(&n,Ainv,&n,ipiv,work,&lwork,&info);
    free(work); free(ipiv);
    return info;
}

int la_logdet(int n,double *A,int *sign,double *logabs){
    int *ipiv=malloc(sizeof(int)*n); int info;
    dgetrf_(&n,&n,A,&n,ipiv,&info);
    if(info!=0){ free(ipiv); return info; }
    int s=1; double la=0.0;
    for(int i=0;i<n;i++){
        double d=A[i+i*n];
        if(d<0) s=-s;
        la+=log(fabs(d));
        if(ipiv[i]!=i+1) s=-s;   /* Fortran 1-based pivot */
    }
    *sign=s; *logabs=la; free(ipiv);
    return 0;
}

void la_expm_sym(int n,const double *A,double scale,double *expA){
    double *V=malloc(sizeof(double)*n*n);
    double *w=malloc(sizeof(double)*n);
    memcpy(V,A,sizeof(double)*n*n);
    int lwork=n*64,info; double *work=malloc(sizeof(double)*lwork);
    dsyev_("V","U",&n,V,&n,w,work,&lwork,&info);   /* V columns = eigenvectors */
    /* expA = V diag(exp(scale*w)) V^T */
    double *VD=malloc(sizeof(double)*n*n);
    for(int j=0;j<n;j++){
        double f=exp(scale*w[j]);
        for(int i=0;i<n;i++) VD[i+j*n]=V[i+j*n]*f;
    }
    la_gemm(n,0,1,1.0,VD,V,0.0,expA);              /* VD * V^T */
    free(V);free(w);free(work);free(VD);
}
```

- [ ] **Step 4: テストが通ることを確認**

Run: `make tests/test_linalg && ./tests/test_linalg`
Expected: `OK`

- [ ] **Step 5: Commit**

```bash
git add src/linalg.h src/linalg.c tests/test_linalg.c
git commit -m "feat(linalg): matmul, inverse, logdet, symmetric expm"
```

---

## Task 2: linalg — UDV 安定化による (I + 行列積)^{-1}

低温で `B_L⋯B_1` の積が指数的に悪条件化するのを防ぐ。チェーンを `U·diag(D)·T`（U: 直交, D: スケール, T: 単位上三角の積）に QR で分解し、安定な公式で `(I+UDT)^{-1}` を計算する。

公式: `D = D_b D_s`（`D_b[i]=D[i] if |D[i]|>1 else 1`、`D_s[i]=D[i] else 1`）とすると
`(I+UDT)^{-1} = T^{-1} M^{-1} D_b^{-1} U^T`、`M = D_b^{-1} U^T T^{-1} + D_s`。

**Files:**
- Modify: `src/linalg.h`, `src/linalg.c`
- Test: `tests/test_udv.c`

- [ ] **Step 1: ヘッダに追加**

Append to `src/linalg.h` (before `#endif`):

```c
/* UDV 蓄積器: 部分積 P = U * diag(D) * T を保持 */
typedef struct { int n; double *U; double *D; double *T; } UDV;
void udv_init(UDV *s, int n);            /* P = I に初期化 */
void udv_free(UDV *s);
void udv_lmul(UDV *s, const double *B);  /* P <- B * P をQR安定化で更新 */
/* g = (I + P)^{-1} を安定公式で計算（P = U D T）。返り値 0=成功 */
int  udv_inv_one_plus(const UDV *s, double *g);
```

- [ ] **Step 2: 失敗するテストを書く**

Create `tests/test_udv.c` — 適度な条件数の行列で「素朴な (I+B3 B2 B1)^{-1}」と UDV 版が一致することを確認:

```c
#include "test_util.h"
#include "linalg.h"
#include <stdlib.h>
#include <string.h>
int main(void){
    int n=3;
    double B1[9]={1.2,0.1,0.0, 0.3,0.9,0.2, 0.0,0.1,1.1};
    double B2[9]={0.8,0.0,0.2, 0.1,1.3,0.0, 0.3,0.2,0.7};
    double B3[9]={1.0,0.2,0.1, 0.0,0.6,0.3, 0.2,0.0,1.4};
    /* 素朴: P = B3*B2*B1 */
    double t[9],P[9];
    la_matmul(n,B3,B2,t); la_matmul(n,t,B1,P);
    double IpP[9]; for(int i=0;i<9;i++) IpP[i]=P[i];
    for(int i=0;i<n;i++) IpP[i+i*n]+=1.0;
    double gref[9]; CHECK(la_inverse(n,IpP,gref)==0);
    /* UDV: lmul の順序は B1,B2,B3（P=B3 B2 B1 になるよう P<-B*P） */
    UDV s; udv_init(&s,n);
    udv_lmul(&s,B1); udv_lmul(&s,B2); udv_lmul(&s,B3);
    double g[9]; CHECK(udv_inv_one_plus(&s,g)==0);
    for(int i=0;i<9;i++) CHECK_CLOSE(g[i],gref[i],1e-9);
    udv_free(&s);

    /* stress: 強いスケール分離で D_b/D_s 分割と diag(1/Db) の列作用を検証。
       P = S2 * Mix * S1。素朴逆行列比較ではなく residual (I+P)g≈I を見る。 */
    double S1[9]={1e4,0,0, 0,1.0,0, 0,0,1e-4};
    double Mix[9]={1.0,0.2,-0.1, -0.3,1.0,0.4, 0.15,-0.25,1.0};
    double S2[9]={1e-3,0,0, 0,1e2,0, 0,0,2.0};
    UDV st; udv_init(&st,n);
    udv_lmul(&st,S1); udv_lmul(&st,Mix); udv_lmul(&st,S2);
    double gst[9]; CHECK(udv_inv_one_plus(&st,gst)==0);
    double tmpa[9], Pst[9], Ip[9], R[9];
    la_matmul(n,Mix,S1,tmpa);
    la_matmul(n,S2,tmpa,Pst);
    for(int i=0;i<9;i++) Ip[i]=Pst[i];
    for(int i=0;i<n;i++) Ip[i+i*n]+=1.0;
    la_matmul(n,Ip,gst,R);
    for(int j=0;j<n;j++) for(int i=0;i<n;i++) CHECK_CLOSE(R[i+j*n],(i==j)?1.0:0.0,1e-6);
    udv_free(&st);
    TEST_END();
}
```

- [ ] **Step 3: 実装を追加**

Append to `src/linalg.c` (追加の extern 宣言をファイル上部の extern 群に加える):

```c
extern void dgeqrf_(const int*,const int*,double*,const int*,double*,double*,const int*,int*);
extern void dorgqr_(const int*,const int*,const int*,double*,const int*,const double*,double*,const int*,int*);
```

Append (関数本体):

```c
void udv_init(UDV *s,int n){
    s->n=n;
    s->U=malloc(sizeof(double)*n*n);
    s->D=malloc(sizeof(double)*n);
    s->T=malloc(sizeof(double)*n*n);
    la_eye(n,s->U); la_eye(n,s->T);
    for(int i=0;i<n;i++) s->D[i]=1.0;
}
void udv_free(UDV *s){ free(s->U); free(s->D); free(s->T); }

/* P <- B*P. 現 P=U D T。 M = (B U) diag(D); QR(M)=Q R;
   U'=Q; D'=diag(R); T' = (diag(1/D') R) T */
void udv_lmul(UDV *s,const double *B){
    int n=s->n;
    double *M=malloc(sizeof(double)*n*n);
    la_matmul(n,B,s->U,M);                 /* M = B*U */
    for(int j=0;j<n;j++) for(int i=0;i<n;i++) M[i+j*n]*=s->D[j]; /* *diag(D) */
    /* QR */
    double *tau=malloc(sizeof(double)*n);
    int lwork=n*64,info; double *work=malloc(sizeof(double)*lwork);
    double *R=malloc(sizeof(double)*n*n);
    dgeqrf_(&n,&n,M,&n,tau,work,&lwork,&info);
    /* R = 上三角部 of M */
    memset(R,0,sizeof(double)*n*n);
    for(int j=0;j<n;j++) for(int i=0;i<=j;i++) R[i+j*n]=M[i+j*n];
    /* Q を M に展開 */
    dorgqr_(&n,&n,&n,M,&n,tau,work,&lwork,&info);
    memcpy(s->U,M,sizeof(double)*n*n);     /* U'=Q */
    /* D'=diag(R), Rn = diag(1/D') R */
    for(int i=0;i<n;i++) s->D[i]=R[i+i*n];
    for(int j=0;j<n;j++){ for(int i=0;i<n;i++){ double d=s->D[i]; R[i+j*n]= (i<=j)? R[i+j*n]/d : 0.0; } }
    /* T' = Rn * T */
    double *Tnew=malloc(sizeof(double)*n*n);
    la_matmul(n,R,s->T,Tnew);
    memcpy(s->T,Tnew,sizeof(double)*n*n);
    free(M);free(tau);free(work);free(R);free(Tnew);
}

int udv_inv_one_plus(const UDV *s,double *g){
    int n=s->n;
    double *Db=malloc(sizeof(double)*n), *Ds=malloc(sizeof(double)*n);
    for(int i=0;i<n;i++){
        if(fabs(s->D[i])>1.0){ Db[i]=s->D[i]; Ds[i]=1.0; }
        else { Db[i]=1.0; Ds[i]=s->D[i]; }
    }
    /* Tinv = T^{-1} */
    double *Tinv=malloc(sizeof(double)*n*n);
    if(la_inverse(n,s->T,Tinv)!=0){ free(Db);free(Ds);free(Tinv); return 1; }
    /* M = diag(1/Db) * U^T * Tinv + diag(Ds) */
    double *UT_Tinv=malloc(sizeof(double)*n*n);
    la_gemm(n,1,0,1.0,s->U,Tinv,0.0,UT_Tinv);   /* U^T * Tinv */
    double *M=malloc(sizeof(double)*n*n);
    for(int j=0;j<n;j++) for(int i=0;i<n;i++) M[i+j*n]=UT_Tinv[i+j*n]/Db[i];
    for(int i=0;i<n;i++) M[i+i*n]+=Ds[i];
    /* Minv */
    double *Minv=malloc(sizeof(double)*n*n);
    if(la_inverse(n,M,Minv)!=0){ free(Db);free(Ds);free(Tinv);free(UT_Tinv);free(M);free(Minv); return 1; }
    /* g = Tinv * Minv * diag(1/Db) * U^T */
    double *DbU=malloc(sizeof(double)*n*n);     /* diag(1/Db) * U^T */
    la_gemm(n,0,1,1.0,Minv,s->U,0.0,DbU);       /* 一時: Minv * U^T （下で行スケール前に）*/
    /* 正しくは: tmp1 = Minv * diag(1/Db) ; g_partial = tmp1 * U^T ; g = Tinv * g_partial */
    double *tmp1=malloc(sizeof(double)*n*n);
    for(int j=0;j<n;j++) for(int i=0;i<n;i++) tmp1[i+j*n]=Minv[i+j*n]/Db[j];
    double *gp=malloc(sizeof(double)*n*n);
    la_gemm(n,0,1,1.0,tmp1,s->U,0.0,gp);        /* tmp1 * U^T */
    la_matmul(n,Tinv,gp,g);                     /* Tinv * gp */
    free(Db);free(Ds);free(Tinv);free(UT_Tinv);free(M);free(Minv);free(DbU);free(tmp1);free(gp);
    return 0;
}
```

> 実装注: `g = T^{-1} M^{-1} diag(1/Db) U^T`。`diag(1/Db)` は `M^{-1}` の**列**に作用するので `tmp1[i+j*n]=Minv[i+j*n]/Db[j]`。`DbU` 行は未使用なので実装時に削除してよい（テスト緑化後の整理ステップ）。

- [ ] **Step 4: テストが通ることを確認**

Run: `make tests/test_udv && ./tests/test_udv`
Expected: `OK`

- [ ] **Step 5: 未使用変数を削除して再確認・Commit**

`DbU` 行（`la_gemm(... DbU)` と宣言・free）を削除し再度 `make tests/test_udv && ./tests/test_udv` で `OK`。

```bash
git add src/linalg.h src/linalg.c tests/test_udv.c
git commit -m "feat(linalg): UDV-stabilized (I+chain)^{-1}"
```

---

## Task 3: lattice — chain / square ジェネレータ

**Files:**
- Create: `src/lattice.h`, `src/lattice.c`
- Test: `tests/test_lattice.c`

- [ ] **Step 1: ヘッダを書く**

Create `src/lattice.h`:

```c
#ifndef LATTICE_H
#define LATTICE_H
typedef enum { LAT_CHAIN, LAT_SQUARE } LatType;
typedef struct {
    int n;            /* サイト数 */
    double *t;        /* n*n ホッピング行列 (column-major)。H = sum t_ij c_i^† c_j */
    int *bipart;      /* 各サイトの副格子符号 ±1（二部格子のとき） */
    int is_bipartite; /* 1=二部格子（半充填で符号問題なしを保証）, 0=非二部 */
} Lattice;
/* chain: Lx サイト, 最近接 hopping 振幅 thop (符号込み: 標準は -t)、pbc=1で周期境界。
   PBC かつ Lx>1 で Lx 奇数 → 非二部（Lx==1 は self-bond を作らないので二部） */
void lattice_chain(Lattice *L, int Lx, double thop, int pbc);
/* square: Lx*Ly, x/y 最近接 hopping thop, pbc。
   PBC の実 bond がある方向（Lx>1 または Ly>1）が奇数なら非二部。
   Lx==1/Ly==1 の縮退方向は self-bond を作らないので二部性判定から除外する。 */
void lattice_square(Lattice *L, int Lx, int Ly, double thop, int pbc);
void lattice_free(Lattice *L);
#endif
```

- [ ] **Step 2: 失敗するテストを書く**

Create `tests/test_lattice.c`:

```c
#include "test_util.h"
#include "lattice.h"
int main(void){
    Lattice L;
    lattice_chain(&L,4,-1.0,1);          /* 4-site ring, t=-1 */
    CHECK(L.n==4);
    /* 各サイトは2近接、t_ij=-1。行和（非対角）= -2 */
    for(int i=0;i<4;i++){ double s=0; for(int j=0;j<4;j++) if(j!=i) s+=L.t[i+j*4]; CHECK_CLOSE(s,-2.0,1e-12); }
    /* 対称性 */
    for(int i=0;i<4;i++) for(int j=0;j<4;j++) CHECK_CLOSE(L.t[i+j*4],L.t[j+i*4],1e-12);
    /* 二部符号: 交互 */
    CHECK(L.bipart[0]*L.bipart[1]==-1);
    CHECK(L.is_bipartite==1);             /* 4-chain PBC は二部 */
    lattice_free(&L);

    /* 二部性チェック: 偶数/奇数で is_bipartite が切り替わる */
    Lattice c3; lattice_chain(&c3,3,-1.0,1); CHECK(c3.is_bipartite==0); lattice_free(&c3); /* 3-chain PBC 非二部 */
    Lattice c4o; lattice_chain(&c4o,3,-1.0,0); CHECK(c4o.is_bipartite==1); lattice_free(&c4o); /* OBC は常に二部 */
    Lattice c1; lattice_chain(&c1,1,-1.0,1); CHECK(c1.is_bipartite==1); CHECK_CLOSE(c1.t[0],0.0,1e-12); lattice_free(&c1); /* 1-site PBC は self-bond なし */
    Lattice s44; lattice_square(&s44,4,4,-1.0,1); CHECK(s44.is_bipartite==1); lattice_free(&s44); /* 4x4 PBC 二部 */
    Lattice s34; lattice_square(&s34,3,4,-1.0,1); CHECK(s34.is_bipartite==0); lattice_free(&s34); /* 3x4 PBC 非二部 */

    Lattice S; lattice_square(&S,2,2,-1.0,1);
    CHECK(S.n==4);
    /* 2x2 PBC は同一ペアが二重隣接する縮退ケース。n と対称性のみ検証。
       定量検証(ED照合)には使わない（下の注を参照）。 */
    for(int i=0;i<4;i++) for(int j=0;j<4;j++) CHECK_CLOSE(S.t[i+j*4],S.t[j+i*4],1e-12);
    lattice_free(&S);

    /* square で Ly==1 PBC の self-bond が入らない（対角=0）ことを確認 */
    Lattice s1; lattice_square(&s1,3,1,-1.0,1);
    for(int i=0;i<3;i++) CHECK_CLOSE(s1.t[i+i*3],0.0,1e-12);
    lattice_free(&s1);
    Lattice s14; lattice_square(&s14,1,4,-1.0,1);
    CHECK(s14.is_bipartite==1);             /* x 方向は self-bond なし、実質 4-site chain PBC */
    for(int i=0;i<4;i++) CHECK_CLOSE(s14.t[i+i*4],0.0,1e-12);
    lattice_free(&s14);
    TEST_END();
}
```

> 注: 2×2 PBC は同一ペアが前後で二重隣接する縮退ケース（`t_ij=-2` の bond が生じる）。実装は「方向ごとに +thop を加算」する素直な規約のままとするが、**ED/TPQ との定量比較には 2×2 PBC を使わない**。2D の grand-canonical ED 照合は `2×2/2×3/2×4 OBC` などの小 OBC 系を用いる。`4×4` は grand-canonical ED には大きすぎるため TPQ・将来検証候補として扱う。ED 側には DQMC が構築した hopping 行列をそのまま渡す（Task 14 の hopping dump を利用）。`stab` 等とは無関係。

- [ ] **Step 3: 実装を書く**

Create `src/lattice.c`:

```c
#include "lattice.h"
#include <stdlib.h>
#include <string.h>

static void alloc_lat(Lattice *L,int n){
    L->n=n;
    L->t=calloc((size_t)n*n,sizeof(double));
    L->bipart=calloc(n,sizeof(int));
}
static void add_bond(Lattice *L,int i,int j,double thop){
    L->t[i+j*L->n]+=thop; L->t[j+i*L->n]+=thop;
}
void lattice_chain(Lattice *L,int Lx,double thop,int pbc){
    alloc_lat(L,Lx);
    for(int x=0;x<Lx;x++){
        int xr=x+1;
        if(xr<Lx) add_bond(L,x,xr,thop);
        else if(pbc&&Lx>1) add_bond(L,x,0,thop);   /* Lx==1 の self-bond を回避 */
        L->bipart[x] = (x%2==0)? +1 : -1;
    }
    /* PBC で Lx>1 の奇数リングは二部にできない。Lx==1 は self-bond を作らないので二部。 */
    L->is_bipartite = (pbc && Lx>1 && (Lx%2!=0)) ? 0 : 1;
}
void lattice_square(Lattice *L,int Lx,int Ly,double thop,int pbc){
    int n=Lx*Ly; alloc_lat(L,n);
    #define IDX(x,y) ((x)+(y)*Lx)
    for(int y=0;y<Ly;y++) for(int x=0;x<Lx;x++){
        int i=IDX(x,y);
        /* PBC の wrap は Lx>1 / Ly>1 のときのみ（L==1 だと自分自身への self-bond になる） */
        int xr=x+1; if(xr<Lx) add_bond(L,i,IDX(xr,y),thop); else if(pbc&&Lx>1) add_bond(L,i,IDX(0,y),thop);
        int yr=y+1; if(yr<Ly) add_bond(L,i,IDX(x,yr),thop); else if(pbc&&Ly>1) add_bond(L,i,IDX(x,0),thop);
        L->bipart[i] = ((x+y)%2==0)? +1 : -1;
    }
    #undef IDX
    /* PBC では実際に wrap bond がある方向（Lx>1/Ly>1）が奇数なら非二部 */
    int odd = (pbc && Lx>1 && (Lx%2!=0)) || (pbc && Ly>1 && (Ly%2!=0));
    L->is_bipartite = odd ? 0 : 1;
}
void lattice_free(Lattice *L){ free(L->t); free(L->bipart); }
```

- [ ] **Step 4: テスト確認**

Run: `make tests/test_lattice && ./tests/test_lattice`
Expected: `OK`

- [ ] **Step 5: Commit**

```bash
git add src/lattice.h src/lattice.c tests/test_lattice.c
git commit -m "feat(lattice): chain and square hopping-matrix generators"
```

---

## Task 4: model — K_σ 構築と expK 前計算

半充填では `μ=U/2`、`K_σ = t_ij + δ_ij(−μ)`（A.9、乱れ w_i=0）。半充填・非対称形では K は両スピン共通（磁場なし）。

**Files:**
- Create: `src/model.h`, `src/model.c`
- Test: `tests/test_model.c`

- [ ] **Step 1: ヘッダ**

Create `src/model.h`:

```c
#ifndef MODEL_H
#define MODEL_H
#include "lattice.h"
typedef struct {
    int n;
    double U, mu, dtau;
    double *K;        /* n*n: t_ij + (-mu) on diagonal */
    double *expK;     /* exp(-dtau*K) */
    double *expKinv;  /* exp(+dtau*K) */
} Model;
/* half=1 なら mu=U/2 を自動設定。lattice の t を借用してKを構築。 */
void model_init(Model *m, const Lattice *L, double U, double dtau, int half, double mu_in);
void model_free(Model *m);
#endif
```

- [ ] **Step 2: 失敗するテスト**

Create `tests/test_model.c`:

```c
#include "test_util.h"
#include "lattice.h"
#include "model.h"
#include "linalg.h"
int main(void){
    Lattice L; lattice_chain(&L,4,-1.0,1);
    Model m; model_init(&m,&L,4.0,0.1,1,0.0);
    CHECK_CLOSE(m.mu,2.0,1e-12);                 /* half filling */
    /* 対角に -mu */
    for(int i=0;i<4;i++) CHECK_CLOSE(m.K[i+i*4], -2.0, 1e-12);
    /* expK * expKinv = I */
    double P[16]; la_matmul(4,m.expK,m.expKinv,P);
    for(int i=0;i<4;i++) for(int j=0;j<4;j++) CHECK_CLOSE(P[i+j*4],(i==j)?1.0:0.0,1e-10);
    model_free(&m); lattice_free(&L);
    TEST_END();
}
```

- [ ] **Step 3: 実装**

Create `src/model.c`:

```c
#include "model.h"
#include "linalg.h"
#include <stdlib.h>
#include <string.h>
void model_init(Model *m,const Lattice *L,double U,double dtau,int half,double mu_in){
    int n=L->n; m->n=n; m->U=U; m->dtau=dtau;
    m->mu = half ? U/2.0 : mu_in;
    m->K=malloc(sizeof(double)*n*n);
    memcpy(m->K,L->t,sizeof(double)*n*n);
    for(int i=0;i<n;i++) m->K[i+i*n]+= -m->mu;
    m->expK=malloc(sizeof(double)*n*n);
    m->expKinv=malloc(sizeof(double)*n*n);
    la_expm_sym(n,m->K,-dtau,m->expK);
    la_expm_sym(n,m->K,+dtau,m->expKinv);
}
void model_free(Model *m){ free(m->K); free(m->expK); free(m->expKinv); }
```

- [ ] **Step 4: テスト確認**

Run: `make tests/test_model && ./tests/test_model`
Expected: `OK`

- [ ] **Step 5: Commit**

```bash
git add src/model.h src/model.c tests/test_model.c
git commit -m "feat(model): K_sigma build + expK precompute (A.9)"
```

---

## Task 5: field — 補助場・λ・N_imσ

`cosh λ = exp(ΔτU/2)` (A.11)、`N_imσ = exp(−2λσ s) − 1` (A.93)。場 `s[l*n+i] ∈ {±1}`。

**Files:**
- Create: `src/field.h`, `src/field.c`, `src/rng.h`, `src/rng.c`
- Test: `tests/test_field.c`

- [ ] **Step 1: rng ヘッダ・実装（xoshiro256**）**

Create `src/rng.h`:

```c
#ifndef RNG_H
#define RNG_H
#include <stdint.h>
typedef struct { uint64_t s[4]; } Rng;
void   rng_seed(Rng *r, uint64_t seed);
double rng_double(Rng *r);   /* [0,1) */
#endif
```

Create `src/rng.c`:

```c
#include "rng.h"
static uint64_t rotl(uint64_t x,int k){ return (x<<k)|(x>>(64-k)); }
static uint64_t splitmix(uint64_t *x){
    uint64_t z=(*x+=0x9E3779B97F4A7C15ULL);
    z=(z^(z>>30))*0xBF58476D1CE4E5B9ULL;
    z=(z^(z>>27))*0x94D049BB133111EBULL;
    return z^(z>>31);
}
void rng_seed(Rng *r,uint64_t seed){ for(int i=0;i<4;i++) r->s[i]=splitmix(&seed); }
double rng_double(Rng *r){
    uint64_t *s=r->s;
    uint64_t res=rotl(s[1]*5,7)*9;
    uint64_t t=s[1]<<17;
    s[2]^=s[0]; s[3]^=s[1]; s[1]^=s[2]; s[0]^=s[3]; s[2]^=t; s[3]=rotl(s[3],45);
    return (res>>11)*(1.0/9007199254740992.0);
}
```

- [ ] **Step 2: field ヘッダ**

Create `src/field.h`:

```c
#ifndef FIELD_H
#define FIELD_H
#include "rng.h"
typedef struct {
    int n, L;       /* サイト数, トロッター層数 */
    double lambda;  /* acosh(exp(dtau*U/2)) */
    signed char *s; /* L*n, 値 ±1。s[l*n+i] */
} Field;
void   field_init(Field *f, int n, int L, double U, double dtau, Rng *r);
void   field_free(Field *f);
/* 反転 s->-s の差分係数 N_imσ = exp(-2*lambda*sigma*s_il) - 1 （付録A.93）。
   引数 s_il は **反転前** の補助場。よって反転後の場 s_now を持っている呼び出し側は
   field_N(f, sigma, -s_now) を渡すこと（s_now = -s_old のため）。 */
double field_N(const Field *f, double sigma, signed char s_il);
#endif
```

- [ ] **Step 3: 失敗するテスト**

Create `tests/test_field.c`:

```c
#include "test_util.h"
#include "field.h"
#include <math.h>
int main(void){
    Rng r; rng_seed(&r,12345);
    Field f; field_init(&f,4,10,4.0,0.1,&r);
    CHECK_CLOSE(cosh(f.lambda),exp(0.1*4.0/2.0),1e-12);   /* A.11 */
    /* N(σ=+1, s=+1) = exp(-2λ) - 1 */
    CHECK_CLOSE(field_N(&f,+1.0,+1), exp(-2*f.lambda)-1.0, 1e-12);
    CHECK_CLOSE(field_N(&f,-1.0,+1), exp(+2*f.lambda)-1.0, 1e-12);
    /* 全要素が ±1 */
    for(int k=0;k<f.L*f.n;k++) CHECK(f.s[k]==1||f.s[k]==-1);
    field_free(&f);
    TEST_END();
}
```

- [ ] **Step 4: 実装**

Create `src/field.c`:

```c
#include "field.h"
#include <stdlib.h>
#include <math.h>
void field_init(Field *f,int n,int L,double U,double dtau,Rng *r){
    f->n=n; f->L=L;
    f->lambda=acosh(exp(dtau*U/2.0));
    f->s=malloc((size_t)L*n);
    for(int k=0;k<L*n;k++) f->s[k]= (rng_double(r)<0.5)? -1 : +1;
}
void field_free(Field *f){ free(f->s); }
double field_N(const Field *f,double sigma,signed char s_il){
    return exp(-2.0*f->lambda*sigma*(double)s_il) - 1.0;
}
```

- [ ] **Step 5: テスト確認・Commit**

Run: `make tests/test_field && ./tests/test_field`
Expected: `OK`

```bash
git add src/field.h src/field.c src/rng.h src/rng.c tests/test_field.c
git commit -m "feat(field,rng): HS lambda, N_imsigma, reproducible RNG"
```

---

## Task 6: green — Bσ行列と Green 初期化（定義 A.95）

`B_lσ = expK · diag(exp(λσ s_il − ΔτU/2))`（A.22 の V を対角指数として適用）。
`g_σ = (I + B_{Lσ}⋯B_{1σ})^{-1}`（A.95）を UDV で安定計算。

**U=0 検証**: U=0 では λ=0, ΔτU/2=0 → `B=expK`、`g=(I+e^{-βK})^{-1}`。

**Files:**
- Create: `src/green.h`, `src/green.c`
- Test: `tests/test_green_init.c`

- [ ] **Step 1: ヘッダ**

Create `src/green.h`:

```c
#ifndef GREEN_H
#define GREEN_H
#include "model.h"
#include "field.h"
typedef struct {
    int n, L;
    const Model *m;
    const Field *f;
    double *g;     /* n*n 等時刻グリーン関数（現在のスライス l に対応） */
    int cur_l;     /* 現在のスライス（0..L-1） */
    double sigma;  /* このグリーンのスピン (+1/-1) */
} Green;
void green_alloc(Green *G, const Model *m, const Field *f, double sigma);
void green_free(Green *G);
/* B_{lσ} を out(n*n) に構築。 */
void green_build_B(const Green *G, int l, double *out);
/* スライス l0 における g を定義(A.95)から UDV 安定化で計算し、G->g, G->cur_l を設定 */
void green_from_scratch(Green *G, int l0);
#endif
```

- [ ] **Step 2: 失敗するテスト（U=0 自由電子）**

Create `tests/test_green_init.c`:

```c
#include "test_util.h"
#include "lattice.h"
#include "model.h"
#include "field.h"
#include "green.h"
#include "linalg.h"
#include <stdlib.h>
#include <string.h>
int main(void){
    int Lx=4, Ltr=20; double dtau=0.1; /* beta=2.0 */
    Lattice L; lattice_chain(&L,Lx,-1.0,1);
    Model m; model_init(&m,&L,0.0,dtau,1,0.0);   /* U=0 → mu=0 */
    Rng r; rng_seed(&r,7);
    Field f; field_init(&f,Lx,Ltr,0.0,dtau,&r);
    Green G; green_alloc(&G,&m,&f,+1.0);
    green_from_scratch(&G,0);
    /* 参照: g = (I + e^{-beta*K})^{-1}, beta=Ltr*dtau */
    double beta=Ltr*dtau;
    double *eK=malloc(sizeof(double)*Lx*Lx);
    la_expm_sym(Lx,m.K,-beta,eK);
    double *IpB=malloc(sizeof(double)*Lx*Lx);
    for(int i=0;i<Lx*Lx;i++) IpB[i]=eK[i];
    for(int i=0;i<Lx;i++) IpB[i+i*Lx]+=1.0;
    double *gref=malloc(sizeof(double)*Lx*Lx);
    la_inverse(Lx,IpB,gref);
    for(int i=0;i<Lx*Lx;i++) CHECK_CLOSE(G.g[i],gref[i],1e-9);
    free(eK);free(IpB);free(gref);
    green_free(&G); field_free(&f); model_free(&m); lattice_free(&L);

    /* U>0: B_l が非可換なケースで green_from_scratch の巡回積順序を独立検証する。
       参照は B_l を明示的に積んだ P から g_ref=(I+P)^{-1} を作る。 */
    int Ltr2=5; double U=4.0;
    Lattice L2; lattice_chain(&L2,Lx,-1.0,1);
    Model m2; model_init(&m2,&L2,U,dtau,1,0.0);
    Rng r2; rng_seed(&r2,13);
    Field f2; field_init(&f2,Lx,Ltr2,U,dtau,&r2);
    Green G2; green_alloc(&G2,&m2,&f2,+1.0);
    double *B=malloc(sizeof(double)*Lx*Lx);
    double *P=malloc(sizeof(double)*Lx*Lx);
    double *tmp=malloc(sizeof(double)*Lx*Lx);
    double *Ip=malloc(sizeof(double)*Lx*Lx);
    double *g2ref=malloc(sizeof(double)*Lx*Lx);
    for(int l0=0;l0<2;l0++){
        green_from_scratch(&G2,l0);
        la_eye(Lx,P);
        for(int k=0;k<Ltr2;k++){
            int l=(l0+k)%Ltr2;          /* 右端から左へ: B_l P */
            green_build_B(&G2,l,B);
            la_matmul(Lx,B,P,tmp);
            memcpy(P,tmp,sizeof(double)*Lx*Lx);
        }
        for(int i=0;i<Lx*Lx;i++) Ip[i]=P[i];
        for(int i=0;i<Lx;i++) Ip[i+i*Lx]+=1.0;
        CHECK(la_inverse(Lx,Ip,g2ref)==0);
        for(int i=0;i<Lx*Lx;i++) CHECK_CLOSE(G2.g[i],g2ref[i],1e-9);
    }
    free(B);free(P);free(tmp);free(Ip);free(g2ref);
    green_free(&G2); field_free(&f2); model_free(&m2); lattice_free(&L2);
    TEST_END();
}
```

- [ ] **Step 3: 実装**

Create `src/green.c`:

```c
#include "green.h"
#include "linalg.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

void green_alloc(Green *G,const Model *m,const Field *f,double sigma){
    G->n=m->n; G->L=f->L; G->m=m; G->f=f; G->sigma=sigma; G->cur_l=0;
    G->g=malloc(sizeof(double)*G->n*G->n);
}
void green_free(Green *G){ free(G->g); }

/* B_l = expK * diag(exp(lambda*sigma*s_il - dtau*U/2)) */
void green_build_B(const Green *G,int l,double *out){
    int n=G->n;
    double *d=malloc(sizeof(double)*n);
    double c=-G->m->dtau*G->m->U/2.0;
    for(int i=0;i<n;i++){
        signed char s=G->f->s[l*n+i];
        d[i]=exp(G->f->lambda*G->sigma*(double)s + c);
    }
    /* out = expK * diag(d): 列 j を d[j] 倍 */
    for(int j=0;j<n;j++) for(int i=0;i<n;i++) out[i+j*n]=G->m->expK[i+j*n]*d[j];
    free(d);
}

/* g_{l0} = (I + B_{l0-1}...B_0 B_{L-1}...B_{l0})^{-1}  （A.95, 巡回順） */
void green_from_scratch(Green *G,int l0){
    int n=G->n, L=G->L;
    UDV s; udv_init(&s,n);
    double *B=malloc(sizeof(double)*n*n);
    /* 積 P = B_{l0-1} ... B_{l0}（右から左）。UDV は P<-B*P なので右端から順に lmul。
       右端 = B_{l0}, ..., 左端 = B_{l0-1}（巡回）。lmul 順 = 右端→左端。 */
    for(int k=0;k<L;k++){
        int l=(l0+k)%L;            /* 右端から: l0, l0+1, ..., l0-1 */
        green_build_B(G,l,B);
        udv_lmul(&s,B);
    }
    udv_inv_one_plus(&s,G->g);
    G->cur_l=l0;
    udv_free(&s); free(B);
}
```

- [ ] **Step 4: テスト確認**

Run: `make tests/test_green_init && ./tests/test_green_init`
Expected: `OK`

> もし不一致なら lmul の巡回順を確認（`l0` を右端、`(l0+k)%L` を左へ積む規約）。U=0 では全 B が等しいので順序非依存になるため、U>0 の明示積テストで `l0=0/1` の巡回順も検証する。

- [ ] **Step 5: Commit**

```bash
git add src/green.h src/green.c tests/test_green_init.c
git commit -m "feat(green): B matrices + stabilized Green init (A.95), U=0 verified"
```

---

## Task 7: green — rank-1 更新（A.107）と受理比（A.99）

**Files:**
- Modify: `src/green.h`, `src/green.c`
- Test: `tests/test_green_update.c`

- [ ] **Step 1: ヘッダに追加**

Append to `src/green.h` (before `#endif`):

```c
/* 反転差分係数 N（付録A.93）。サイト i・現スライス cur_l の **反転前** 補助場から計算:
   N = field_N(σ, s[cur_l*n+i]) = exp(-2λσ s_old) - 1。 */
double green_flipN(const Green *G, int i);
/* 単スピン重み比 R_σ = 1 + (1 - g_ii) * N （A.98）。N は green_flipN で得た反転前ベース。 */
double green_ratio_N(const Green *G, int i, double N);
/* サイト i の反転を g に反映（A.107）。N は green_flipN で得た反転前ベースの同じ値を渡す。
   この関数は field を読まないので、field の反転は呼び出しの前後どちらでもよい。 */
void   green_update(Green *G, int i, double N);
#endif
```
> 既存の `#endif` を上記ブロックで置換する形で追加。
>
> **設計意図（バグ防止）**: `N` は「反転前の補助場」から計算する一意の量で、受理比 (A.98) と
> 更新 (A.107) の両方で同じ値を使う。そこで `N` を `green_flipN` で1回だけ求め、`green_ratio_N`
> と `green_update` に渡す。こうすると「反転前/反転後どちらの場で N を評価するか」という取り違え
> （符号反転バグ）が原理的に起きない。`green_update` は field を参照しないので、実際の `s` 反転は
> update の前でも後でもよい。

- [ ] **Step 2: 失敗するテスト**

「N=反転前ベースで ratio が det 比と一致」かつ「update 後の g が、場を実際に反転して from_scratch した g と一致」を確認。

Create `tests/test_green_update.c`:

```c
#include "test_util.h"
#include "lattice.h"
#include "model.h"
#include "field.h"
#include "green.h"
#include "linalg.h"
#include <stdlib.h>
int main(void){
    int Lx=4, Ltr=8; double dtau=0.1, U=4.0;
    Lattice L; lattice_chain(&L,Lx,-1.0,1);
    Model m; model_init(&m,&L,U,dtau,1,0.0);
    int isite=2;

    /* N の符号は σ に依存するので up/down 両方で検証する */
    for(int si=0; si<2; si++){
        double sigma = (si==0)? +1.0 : -1.0;
        Rng r; rng_seed(&r,3);
        Field f; field_init(&f,Lx,Ltr,U,dtau,&r);
        Green G; green_alloc(&G,&m,&f,sigma);
        green_from_scratch(&G,0);            /* slice 0 */

        /* N は反転前の場から: N = exp(-2λσ s_old) - 1 = field_N(σ, s_old) */
        signed char s_old=f.s[0*Lx+isite];
        double Nexp=field_N(&f,sigma,s_old);
        double Ngot=green_flipN(&G,isite);
        CHECK_CLOSE(Ngot,Nexp,1e-12);

        /* ratio = 1 + (1-g_ii) N と直接式を比較 */
        double Rexp=1.0+(1.0-G.g[isite+isite*Lx])*Nexp;
        double Rgot=green_ratio_N(&G,isite,Ngot);
        CHECK_CLOSE(Rgot,Rexp,1e-12);

        /* update（同じ N を渡す）後 g を、場反転 + from_scratch と比較 */
        green_update(&G,isite,Ngot);         /* g を rank-1 更新（field は未反転でよい） */
        f.s[0*Lx+isite]*=-1;                 /* 場を実際に反転 */
        Green G2; green_alloc(&G2,&m,&f,sigma);
        green_from_scratch(&G2,0);           /* 反転後の場で再計算 */
        for(int k=0;k<Lx*Lx;k++) CHECK_CLOSE(G.g[k],G2.g[k],1e-10);

        green_free(&G2); green_free(&G); field_free(&f);
    }
    model_free(&m); lattice_free(&L);
    TEST_END();
}
```

- [ ] **Step 3: 実装を追加**

Append to `src/green.c`:

```c
/* N = exp(-2λσ s_old) - 1。s_old は現スライスの反転前補助場。 */
double green_flipN(const Green *G,int i){
    int n=G->n;
    signed char s_old=G->f->s[G->cur_l*n+i];
    return field_N(G->f,G->sigma,s_old);
}

/* R_σ = 1 + (1 - g_ii) * N （A.98） */
double green_ratio_N(const Green *G,int i,double N){
    int n=G->n;
    return 1.0 + (1.0 - G->g[i+i*n])*N;
}

/* A.107: g'_{jk} = g_jk - (delta_ij - g_ji) N g_ik / (1 + (1-g_ii) N)
   N は反転前ベース（green_flipN）。field は参照しない。 */
void green_update(Green *G,int i,double N){
    int n=G->n;
    double denom=1.0+(1.0-G->g[i+i*n])*N;
    double *g=G->g;
    /* g_ik = g[i + k*n]; g_ji = g[j + i*n] */
    double *u=malloc(sizeof(double)*n);  /* u_j = delta_ij - g_ji = delta_ij - g[j+i*n] */
    double *v=malloc(sizeof(double)*n);  /* v_k = g_ik = g[i + k*n] */
    for(int j=0;j<n;j++) u[j]=((i==j)?1.0:0.0) - g[j+i*n];
    for(int k=0;k<n;k++) v[k]=g[i+k*n];
    double fac=N/denom;
    for(int k=0;k<n;k++) for(int j=0;j<n;j++) g[j+k*n] -= fac*u[j]*v[k];
    free(u);free(v);
}
```

- [ ] **Step 4: テスト確認**

Run: `make tests/test_green_update && ./tests/test_green_update`
Expected: `OK`

> 不一致時は A.107 の添字（`g_ji` vs `g_ij`、`g_ik`）と規約 `g=⟨c c†⟩` を再確認。残差は本来 ~1e-13（`LOG.md` の Sherman–Morrison 検証参照）。
> 上のテストは `sigma=±1` の両スピンで ratio/update/from_scratch を検証する（N の符号は σ 依存のため）。

- [ ] **Step 5: Commit**

```bash
git add src/green.h src/green.c tests/test_green_update.c
git commit -m "feat(green): rank-1 update (A.107) + accept ratio (A.99)"
```

---

## Task 8: green — wrapping（A.108）

`g_{l+1} = B_l g_l B_l^{-1}`（A.108）。次スライスへ移動。

**Files:**
- Modify: `src/green.h`, `src/green.c`
- Test: `tests/test_green_wrap.c`

- [ ] **Step 1: ヘッダに追加**

Insert before `#endif` of `src/green.h`:

```c
/* 現スライス cur_l から cur_l+1 へ: g <- B_{cur_l} g B_{cur_l}^{-1}, cur_l <- (cur_l+1)%L */
void green_wrap(Green *G);
```

- [ ] **Step 2: 失敗するテスト**

`green_wrap` 後の g が、`from_scratch(cur_l+1)` と一致することを確認。

Create `tests/test_green_wrap.c`:

```c
#include "test_util.h"
#include "lattice.h"
#include "model.h"
#include "field.h"
#include "green.h"
#include <stdlib.h>
int main(void){
    int Lx=4, Ltr=8; double dtau=0.1, U=4.0;
    Lattice L; lattice_chain(&L,Lx,-1.0,1);
    Model m; model_init(&m,&L,U,dtau,1,0.0);
    Rng r; rng_seed(&r,11);
    Field f; field_init(&f,Lx,Ltr,U,dtau,&r);
    Green G; green_alloc(&G,&m,&f,+1.0);
    green_from_scratch(&G,0);
    green_wrap(&G);                      /* -> slice 1 */
    CHECK(G.cur_l==1);
    Green G2; green_alloc(&G2,&m,&f,+1.0);
    green_from_scratch(&G2,1);
    for(int k=0;k<Lx*Lx;k++) CHECK_CLOSE(G.g[k],G2.g[k],1e-9);
    green_free(&G2); green_free(&G); field_free(&f); model_free(&m); lattice_free(&L);
    TEST_END();
}
```

- [ ] **Step 3: 実装を追加**

Append to `src/green.c`:

```c
void green_wrap(Green *G){
    int n=G->n;
    double *B=malloc(sizeof(double)*n*n);
    double *Binv=malloc(sizeof(double)*n*n);
    green_build_B(G,G->cur_l,B);
    la_inverse(n,B,Binv);
    double *tmp=malloc(sizeof(double)*n*n);
    la_matmul(n,B,G->g,tmp);        /* B*g */
    la_matmul(n,tmp,Binv,G->g);     /* (B*g)*Binv */
    G->cur_l=(G->cur_l+1)%G->L;
    free(B);free(Binv);free(tmp);
}
```

- [ ] **Step 4: テスト確認**

Run: `make tests/test_green_wrap && ./tests/test_green_wrap`
Expected: `OK`

- [ ] **Step 5: Commit**

```bash
git add src/green.h src/green.c tests/test_green_wrap.c
git commit -m "feat(green): wrapping to next slice (A.108)"
```

---

## Task 9: measure — エネルギー・二重占有・ジャックナイフ

エネルギーの素片（両スピン、半充填では `g↑=g↓` だが両方を引数に取り一般化）。
規約変換できるよう **素片を個別に出す**（設計書 §5）:
- 運動: `ekin = Σ_σ Σ_ij t_ij (δ_ij − g_ji^σ)`
- 相互作用(非対称形): `eint = U Σ_i (1 − g_ii↑)(1 − g_ii↓)` （= U⟨n↑n↓⟩）
- 粒子数: `ntot = Σ_{i,σ} (1 − g_ii^σ)` （= Σ⟨n↑+n↓⟩）
- 二重占有: `doublon = (1/n) Σ_i (1−g_ii↑)(1−g_ii↓)`
- Hubbard エネルギー: `E = ekin + eint`

規約別の全エネルギーは main 側で素片から再構成する:
- 非対称 Hubbard（μ なし）: `E_hub = ⟨H0⟩ = ekin + eint`
- grand-canonical `H0 − μN`: `E_gc = ekin + eint − μ·ntot`
- 粒子正孔対称形 `U(n↑−½)(n↓−½)`: `E_ph = ekin + eint − (U/2)·ntot + (U/4)·n`

**Files:**
- Create: `src/measure.h`, `src/measure.c`
- Test: `tests/test_measure.c`

- [ ] **Step 1: ヘッダ**

Create `src/measure.h`:

```c
#ifndef MEASURE_H
#define MEASURE_H
#include "lattice.h"
/* g_up, g_dn（同一スライス）から1サンプルのエネルギー素片を計算。
   規約別の全エネルギーは呼び出し側で素片から再構成する。 */
typedef struct {
    double E;        /* ekin + eint （非対称 Hubbard エネルギー, μ なし） */
    double ekin;     /* 運動エネルギー（純ホッピング） */
    double eint;     /* U Σ_i ⟨n↑n↓⟩ */
    double ntot;     /* Σ_{i,σ} ⟨n_iσ⟩ */
    double doublon;  /* (1/n) Σ_i ⟨n↑n↓⟩ */
} MeasSample;
MeasSample measure_sample(int n, const double *thop, double U,
                          const double *g_up, const double *g_dn);
/* ジャックナイフ: x[0..N-1] の平均と誤差 */
void jackknife(const double *x, int N, double *mean, double *err);
#endif
```

- [ ] **Step 2: 失敗するテスト（U=0 自由電子の E(T) 解析値）**

U=0 では g↑=g↓=(I+e^{-βK})^{-1}。期待エネルギー = 2Σ_i ε_i f(ε_i)（ε_i: K=t の固有値, f=1/(1+e^{βε})）。
測定の `Ekin` とこの解析値が一致するか。

Create `tests/test_measure.c`:

```c
#include "test_util.h"
#include "lattice.h"
#include "model.h"
#include "field.h"
#include "green.h"
#include "measure.h"
#include "linalg.h"
#include <stdlib.h>
#include <math.h>
extern void dsyev_(const char*,const char*,const int*,double*,const int*,double*,double*,const int*,int*);
int main(void){
    int Lx=4, Ltr=20; double dtau=0.1; double beta=Ltr*dtau;
    Lattice L; lattice_chain(&L,Lx,-1.0,1);
    Model m; model_init(&m,&L,0.0,dtau,1,0.0);    /* U=0, mu=0, K=t */
    Rng r; rng_seed(&r,5);
    Field f; field_init(&f,Lx,Ltr,0.0,dtau,&r);
    Green Gu; green_alloc(&Gu,&m,&f,+1.0); green_from_scratch(&Gu,0);
    Green Gd; green_alloc(&Gd,&m,&f,-1.0); green_from_scratch(&Gd,0);
    MeasSample s=measure_sample(Lx,L.t,0.0,Gu.g,Gd.g);

    /* 解析: ekin = 2 * sum_i eps_i f(eps_i) */
    double Kc[16]; for(int i=0;i<16;i++) Kc[i]=m.K[i];
    double w[4],work[256]; int n=4,lwork=256,info;
    dsyev_("N","U",&n,Kc,&n,w,work,&lwork,&info);
    double ek=0; for(int i=0;i<4;i++){ double fe=1.0/(1.0+exp(beta*w[i])); ek+=w[i]*fe; }
    ek*=2.0;
    CHECK_CLOSE(s.ekin,ek,1e-8);
    CHECK_CLOSE(s.eint,0.0,1e-12);
    /* 半充填 U=0: 粒子正孔対称スペクトルにより ⟨N⟩ = n_site = 4 (厳密) */
    CHECK_CLOSE(s.ntot,4.0,1e-8);

    green_free(&Gu);green_free(&Gd);field_free(&f);model_free(&m);lattice_free(&L);
    TEST_END();
}
```

- [ ] **Step 3: 実装**

Create `src/measure.c`:

```c
#include "measure.h"
#include <math.h>
MeasSample measure_sample(int n,const double *thop,double U,
                          const double *g_up,const double *g_dn){
    MeasSample r;
    double ekin=0.0;
    /* sum_{ij,sigma} t_ij (delta_ij - g_ji^sigma) */
    for(int j=0;j<n;j++) for(int i=0;i<n;i++){
        double t=thop[i+j*n];
        if(t==0.0) continue;
        double dij=(i==j)?1.0:0.0;
        ekin += t*(dij - g_up[j+i*n]);
        ekin += t*(dij - g_dn[j+i*n]);
    }
    double eint=0.0, doub=0.0, ntot=0.0;
    for(int i=0;i<n;i++){
        double nu=1.0-g_up[i+i*n], nd=1.0-g_dn[i+i*n];
        eint += nu*nd; doub += nu*nd; ntot += nu+nd;
    }
    eint*=U; doub/=n;
    r.ekin=ekin; r.eint=eint; r.ntot=ntot; r.E=ekin+eint; r.doublon=doub;
    return r;
}
void jackknife(const double *x,int N,double *mean,double *err){
    double sum=0; for(int i=0;i<N;i++) sum+=x[i];
    double mu=(N>0)?sum/N:0.0; *mean=mu;
    if(N<2){ *err=0.0; return; }   /* N=1 のゼロ割り回避（誤差は定義不能） */
    double var=0;
    for(int i=0;i<N;i++){ double ji=(sum-x[i])/(N-1); var+=(ji-mu)*(ji-mu); }
    *err=sqrt(var*(N-1)/(double)N);
}
```

- [ ] **Step 4: テスト確認**

Run: `make tests/test_measure && ./tests/test_measure`
Expected: `OK`

- [ ] **Step 5: Commit**

```bash
git add src/measure.h src/measure.c tests/test_measure.c
git commit -m "feat(measure): energy/doublon sample (A.56) + jackknife"
```

---

## Task 10: dqmc — BSS スイープ・Metropolis・符号・安定化スケジュール

**Files:**
- Create: `src/dqmc.h`, `src/dqmc.c`

- [ ] **Step 1: ヘッダ**

Create `src/dqmc.h`:

```c
#ifndef DQMC_H
#define DQMC_H
#include "model.h"
#include "field.h"
#include "green.h"
#include "rng.h"
typedef struct {
    int n, L;
    Model *m; Field *f;
    Green Gu, Gd;
    Rng *rng;
    int stab_interval;   /* 何スライスごとに from_scratch で安定化するか */
    double sign;         /* 現配置の符号（v1 は二部格子のみ実行するため +1 起点で絶対符号） */
} Dqmc;
void dqmc_init(Dqmc *D, Model *m, Field *f, Rng *rng, int stab_interval);
void dqmc_free(Dqmc *D);
/* 1スイープ = 全 L スライス × 全サイトの flip 試行 + wrapping + 周期安定化 */
void dqmc_sweep(Dqmc *D);
#endif
```

- [ ] **Step 2: 実装**

Create `src/dqmc.c`:

```c
#include "dqmc.h"
#include "linalg.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

void dqmc_init(Dqmc *D,Model *m,Field *f,Rng *rng,int stab_interval){
    D->n=m->n; D->L=f->L; D->m=m; D->f=f; D->rng=rng;
    D->stab_interval=stab_interval>0?stab_interval:8;
    green_alloc(&D->Gu,m,f,+1.0);
    green_alloc(&D->Gd,m,f,-1.0);
    green_from_scratch(&D->Gu,0);
    green_from_scratch(&D->Gd,0);
    D->sign=1.0;
}
void dqmc_free(Dqmc *D){ green_free(&D->Gu); green_free(&D->Gd); }

void dqmc_sweep(Dqmc *D){
    int n=D->n, L=D->L;
    for(int l=0;l<L;l++){
        /* 両グリーンは現在 cur_l==l を指している前提 */
        for(int i=0;i<n;i++){
            /* N は反転前の場から1回だけ計算し、ratio と update で共有する */
            double Nu=green_flipN(&D->Gu,i);
            double Nd=green_flipN(&D->Gd,i);
            double R=green_ratio_N(&D->Gu,i,Nu)*green_ratio_N(&D->Gd,i,Nd);
            if(rng_double(D->rng) < fabs(R)){
                green_update(&D->Gu,i,Nu);     /* field 反転前に更新（update は field を読まない） */
                green_update(&D->Gd,i,Nd);
                D->f->s[l*n+i]*=-1;            /* 場を反転 */
                if(R<0) D->sign=-D->sign;
            }
        }
        /* 次スライスへ */
        green_wrap(&D->Gu);
        green_wrap(&D->Gd);
        /* 周期安定化 */
        if(((l+1)%D->stab_interval)==0){
            int nl=(l+1)%L;
            green_from_scratch(&D->Gu,nl);
            green_from_scratch(&D->Gd,nl);
        }
    }
}
```

> 設計注: スイープ後 `cur_l` は 0 に戻る（L 回 wrap）。安定化は wrap 後の新スライスで `from_scratch` し丸め誤差を除去。v1 は Task 11 で non-bipartite を実行エラーにするので、半充填二部格子では `sign` は常に +1 のはず（Task 12 で検証）。non-bipartite を将来サポートする場合は、`D->sign=1.0` では不十分で、初期配置の `det(I+P↑)det(I+P↓)` の符号を計算してから flip の相対符号を掛ける。

- [ ] **Step 3: ビルド確認（リンクのみ）**

Run: `make dqmc 2>&1 | head` — まだ main.c が無ければ次タスクで作る。ここでは `make tests/test_green_wrap` 等が引き続き緑であることを確認:
Run: `make test`
Expected: `ALL TESTS PASSED`

- [ ] **Step 4: Commit**

```bash
git add src/dqmc.h src/dqmc.c
git commit -m "feat(dqmc): BSS sweep, Metropolis, sign, stabilization schedule"
```

---

## Task 11: io + main — 入力パース・ドライバ・温度スキャン

**Files:**
- Create: `src/io.h`, `src/io.c`, `src/main.c`
- Create: `input/1d_L4_U0.txt`, `input/1d_L4_U4.txt`
- Test: `tests/test_io.c`

- [ ] **Step 1: io ヘッダ**

Create `src/io.h`:

```c
#ifndef IO_H
#define IO_H
typedef struct {
    char lattice[16];   /* "chain" | "square" */
    int Lx, Ly, pbc;
    double thop, U, dtau;
    double beta_list[64]; int nbeta;
    int nwarm, nmeas, nbin, stab_interval;
    unsigned long long seed;
} Params;
/* key=value ファイルを読む。返り値0=成功。未指定はデフォルト。 */
int params_read(Params *p, const char *path);
#endif
```

- [ ] **Step 2: io 実装**

Create `src/io.c`:

```c
#include "io.h"
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int parse_int_value(const char *s,int *out){
    char *end=0; errno=0;
    long v=strtol(s,&end,10);
    if(errno||end==s||*end!='\0'||v<INT_MIN||v>INT_MAX) return 1;
    *out=(int)v; return 0;
}
static int parse_ull_value(const char *s,unsigned long long *out){
    char *end=0; errno=0;
    unsigned long long v=strtoull(s,&end,10);
    if(errno||end==s||*end!='\0') return 1;
    *out=v; return 0;
}
static int parse_double_value(const char *s,double *out){
    char *end=0; errno=0;
    double v=strtod(s,&end);
    if(errno||end==s||*end!='\0'||!isfinite(v)) return 1;
    *out=v; return 0;
}
static int parse_beta_list(Params *p,char *val){
    p->nbeta=0;
    char *tok=strtok(val,",");
    if(!tok) return 1;
    while(tok){
        if(p->nbeta>=64) return 2;
        if(parse_double_value(tok,&p->beta_list[p->nbeta]) || p->beta_list[p->nbeta]<=0.0) return 1;
        p->nbeta++;
        tok=strtok(0,",");
    }
    return 0;
}
static void defaults(Params *p){
    strcpy(p->lattice,"chain"); p->Lx=4; p->Ly=1; p->pbc=1;
    p->thop=-1.0; p->U=4.0; p->dtau=0.1;
    p->nbeta=0; p->nwarm=200; p->nmeas=2000; p->nbin=20;
    p->stab_interval=8; p->seed=12345ULL;
}
int params_read(Params *p,const char *path){
    defaults(p);
    FILE *fp=fopen(path,"r"); if(!fp){ perror(path); return 1; }
    #define FAIL(...) do{ fprintf(stderr,__VA_ARGS__); fclose(fp); return 1; }while(0)
    char line[512];
    while(fgets(line,sizeof line,fp)){
        char *h=strchr(line,'#'); if(h)*h=0;
        char key[64],val[256];
        if(sscanf(line,"%63[^= \t]=%255s",key,val)!=2) continue;
        if(!strcmp(key,"lattice")){
            strncpy(p->lattice,val,sizeof p->lattice - 1);
            p->lattice[sizeof p->lattice - 1]='\0';
        }
        else if(!strcmp(key,"Lx")){ if(parse_int_value(val,&p->Lx)) FAIL("ERROR: Lx must be integer (got %s)\n",val); }
        else if(!strcmp(key,"Ly")){ if(parse_int_value(val,&p->Ly)) FAIL("ERROR: Ly must be integer (got %s)\n",val); }
        else if(!strcmp(key,"pbc")){ if(parse_int_value(val,&p->pbc)) FAIL("ERROR: pbc must be 0 or 1 (got %s)\n",val); }
        else if(!strcmp(key,"bc")){        /* alias。typo を OBC として黙って解釈しない */
            if(!strcmp(val,"periodic")||!strcmp(val,"pbc")||!strcmp(val,"1")) p->pbc=1;
            else if(!strcmp(val,"open")||!strcmp(val,"obc")||!strcmp(val,"0")) p->pbc=0;
            else { fprintf(stderr,"ERROR: bc must be periodic/open/pbc/obc/1/0 (got %s)\n",val); fclose(fp); return 1; }
        }
        else if(!strcmp(key,"t")){ if(parse_double_value(val,&p->thop)) FAIL("ERROR: t must be numeric (got %s)\n",val); }
        else if(!strcmp(key,"U")){ if(parse_double_value(val,&p->U)) FAIL("ERROR: U must be numeric (got %s)\n",val); }
        else if(!strcmp(key,"dtau")){ if(parse_double_value(val,&p->dtau)) FAIL("ERROR: dtau must be numeric (got %s)\n",val); }
        else if(!strcmp(key,"nwarm")){ if(parse_int_value(val,&p->nwarm)) FAIL("ERROR: nwarm must be integer (got %s)\n",val); }
        else if(!strcmp(key,"nmeas")){ if(parse_int_value(val,&p->nmeas)) FAIL("ERROR: nmeas must be integer (got %s)\n",val); }
        else if(!strcmp(key,"nbin")){ if(parse_int_value(val,&p->nbin)) FAIL("ERROR: nbin must be integer (got %s)\n",val); }
        else if(!strcmp(key,"stab")||!strcmp(key,"stabilize_interval")){
            if(parse_int_value(val,&p->stab_interval)) FAIL("ERROR: stab must be integer (got %s)\n",val);
        }
        else if(!strcmp(key,"seed")){ if(parse_ull_value(val,&p->seed)) FAIL("ERROR: seed must be unsigned integer (got %s)\n",val); }
        else if(!strcmp(key,"beta_list")){
            int rc=parse_beta_list(p,val);
            if(rc==2) FAIL("ERROR: beta_list has too many entries (max 64)\n");
            if(rc) FAIL("ERROR: beta_list must contain positive numeric values\n");
        }
        else FAIL("ERROR: unknown key %s\n",key);
    }
    fclose(fp);
    #undef FAIL
    if(p->nbeta==0){ p->beta_list[0]=2.0; p->nbeta=1; }
    /* Task 11 時点では組み込み lattice のみ。Task 14 で "file" も許可値へ追加する。 */
    if(strcmp(p->lattice,"chain") && strcmp(p->lattice,"square")){
        fprintf(stderr,"ERROR: lattice must be chain or square (got %s)\n",p->lattice);
        return 1;
    }
    if(p->Lx<=0){ fprintf(stderr,"ERROR: Lx must be > 0\n"); return 1; }
    if(p->Ly<=0){ fprintf(stderr,"ERROR: Ly must be > 0\n"); return 1; }
    if(p->pbc!=0 && p->pbc!=1){ fprintf(stderr,"ERROR: pbc must be 0 or 1\n"); return 1; }
    if(p->U<0.0){ fprintf(stderr,"ERROR: U must be >= 0\n"); return 1; }
    if(p->dtau<=0.0){ fprintf(stderr,"ERROR: dtau must be > 0\n"); return 1; }
    if(p->nwarm<0){ fprintf(stderr,"ERROR: nwarm must be >= 0\n"); return 1; }
    if(p->stab_interval<=0){ fprintf(stderr,"ERROR: stab must be > 0\n"); return 1; }
    /* ジャックナイフは nbin>=2 が必須。各ビンに最低1サンプル要る */
    if(p->nbin<2){ fprintf(stderr,"ERROR: nbin must be >= 2 (got %d)\n",p->nbin); return 1; }
    if(p->nmeas<p->nbin){ fprintf(stderr,"ERROR: nmeas (%d) must be >= nbin (%d)\n",p->nmeas,p->nbin); return 1; }
    if(p->nmeas%p->nbin!=0){ fprintf(stderr,"ERROR: nmeas (%d) must be divisible by nbin (%d)\n",p->nmeas,p->nbin); return 1; }
    return 0;
}
```

- [ ] **Step 3: 入力 parser の失敗テストを書く**

Create `tests/test_io.c`:

```c
#include "test_util.h"
#include "io.h"
#include <stdio.h>

static void write_text(const char *path,const char *text){
    FILE *fp=fopen(path,"w");
    fputs(text,fp);
    fclose(fp);
}

int main(void){
    Params p;
    write_text("/tmp/afqmc_valid.in",
        "lattice=chain\nLx=4\nLy=1\npbc=1\nU=4\ndtau=0.1\nnmeas=20\nnbin=10\n");
    CHECK(params_read(&p,"/tmp/afqmc_valid.in")==0);

    write_text("/tmp/afqmc_unknown.in",
        "lattice=chain\nunknown_key=1\nnmeas=20\nnbin=10\n");
    CHECK(params_read(&p,"/tmp/afqmc_unknown.in")!=0);

    write_text("/tmp/afqmc_badnum.in",
        "lattice=chain\nLx=abc\nnmeas=20\nnbin=10\n");
    CHECK(params_read(&p,"/tmp/afqmc_badnum.in")!=0);

    write_text("/tmp/afqmc_badbin.in",
        "lattice=chain\nnmeas=21\nnbin=10\n");
    CHECK(params_read(&p,"/tmp/afqmc_badbin.in")!=0);

    write_text("/tmp/afqmc_badbeta.in",
        "lattice=chain\nbeta_list=1.0,foo\nnmeas=20\nnbin=10\n");
    CHECK(params_read(&p,"/tmp/afqmc_badbeta.in")!=0);
    TEST_END();
}
```

- [ ] **Step 4: main 実装（温度スキャン）**

Create `src/main.c`:

```c
#include "io.h"
#include "lattice.h"
#include "model.h"
#include "field.h"
#include "dqmc.h"
#include "measure.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int main(int argc,char**argv){
    if(argc<2){ fprintf(stderr,"usage: %s input.txt\n",argv[0]); return 1; }
    Params p; if(params_read(&p,argv[1])) return 1;

    Lattice L;
    if(!strcmp(p.lattice,"square")) lattice_square(&L,p.Lx,p.Ly,p.thop,p.pbc);
    else if(!strcmp(p.lattice,"chain")) lattice_chain(&L,p.Lx,p.thop,p.pbc);
    else { fprintf(stderr,"ERROR: unknown lattice %s\n",p.lattice); return 1; } /* params_read の防御的二重化 */

    /* 半充填の符号フリーは二部格子が前提。v1 は絶対 sign 初期化を持たないため非二部を実行しない。 */
    if(!L.is_bipartite){
        fprintf(stderr,"ERROR: non-bipartite lattice is outside v1 scope; sign column would be only relative without determinant-sign initialization\n");
        lattice_free(&L);
        return 1;
    }

    double mu=p.U/2.0;   /* half-filling (model_init half=1 と一致) */
    printf("# lattice=%s n=%d U=%g mu=%g dtau=%g bipartite=%d\n",
           p.lattice,L.n,p.U,mu,p.dtau,L.is_bipartite);
    printf("# T  E_hub dE_hub  E_gc dE_gc  E_ph dE_ph  ntot dN  doublon dD  sign\n");

    for(int b=0;b<p.nbeta;b++){
        double beta=p.beta_list[b];
        int Ltr=(int)lround(beta/p.dtau);
        /* beta/dtau が非整数なら実効 beta がずれる。閾値超過はエラー、出力は beta_eff を使う */
        if(fabs(beta/p.dtau - (double)Ltr) > 1e-9){
            fprintf(stderr,"ERROR: beta=%g not a multiple of dtau=%g (beta/dtau=%g)\n",
                    beta,p.dtau,beta/p.dtau); return 1;
        }
        double beta_eff=Ltr*p.dtau;
        double T=1.0/beta_eff;

        Model m; model_init(&m,&L,p.U,p.dtau,1,0.0);
        Rng r; rng_seed(&r,p.seed + 1000ULL*b);
        Field f; field_init(&f,L.n,Ltr,p.U,p.dtau,&r);
        Dqmc D; dqmc_init(&D,&m,&f,&r,p.stab_interval);

        for(int w=0;w<p.nwarm;w++) dqmc_sweep(&D);

        int per=p.nmeas/p.nbin; if(per<1) per=1;
        /* 規約別エネルギーと粒子数・二重占有をビン平均（符号付き） */
        double *Ehub=calloc(p.nbin,sizeof(double));
        double *Egc =calloc(p.nbin,sizeof(double));
        double *Eph =calloc(p.nbin,sizeof(double));
        double *Nbin=calloc(p.nbin,sizeof(double));
        double *Dbin=calloc(p.nbin,sizeof(double));
        double *Sbin=calloc(p.nbin,sizeof(double));
        for(int bi=0;bi<p.nbin;bi++){
            double eh=0,eg=0,ep=0,nn=0,dd=0,ss=0;
            for(int k=0;k<per;k++){
                dqmc_sweep(&D);
                /* 測定はスライス0で（sweep後 cur_l==0、from_scratchで純化） */
                green_from_scratch(&D.Gu,0);
                green_from_scratch(&D.Gd,0);
                MeasSample s=measure_sample(L.n,L.t,p.U,D.Gu.g,D.Gd.g);
                double e_hub=s.E;                              /* ekin+eint */
                double e_gc =s.E - mu*s.ntot;                  /* H - muN */
                double e_ph =s.E - 0.5*p.U*s.ntot + 0.25*p.U*L.n; /* U(n-1/2)(n-1/2) 形 */
                double sg=D.sign;
                eh+=sg*e_hub; eg+=sg*e_gc; ep+=sg*e_ph;
                nn+=sg*s.ntot; dd+=sg*s.doublon; ss+=sg;
            }
            Ehub[bi]=eh/ss; Egc[bi]=eg/ss; Eph[bi]=ep/ss;
            Nbin[bi]=nn/ss; Dbin[bi]=dd/ss; Sbin[bi]=ss/per;
        }
        double Eh,Ehe,Eg,Ege,Ep,Epe,Nm,Ne,Dm,De,Sm,Se;
        jackknife(Ehub,p.nbin,&Eh,&Ehe);
        jackknife(Egc ,p.nbin,&Eg,&Ege);
        jackknife(Eph ,p.nbin,&Ep,&Epe);
        jackknife(Nbin,p.nbin,&Nm,&Ne);
        jackknife(Dbin,p.nbin,&Dm,&De);
        jackknife(Sbin,p.nbin,&Sm,&Se);
        printf("%.6g %.8g %.3g %.8g %.3g %.8g %.3g %.6g %.2g %.8g %.2g %.6g\n",
               T,Eh,Ehe,Eg,Ege,Ep,Epe,Nm,Ne,Dm,De,Sm);
        fflush(stdout);

        free(Ehub);free(Egc);free(Eph);free(Nbin);free(Dbin);free(Sbin);
        dqmc_free(&D); field_free(&f); model_free(&m);
    }
    lattice_free(&L);
    return 0;
}
```

> 出力列: `T  E_hub dE_hub  E_gc dE_gc  E_ph dE_ph  ntot dN  doublon dD  sign`。
> `E_hub`=⟨H0⟩=ekin+eint（μ なし）、`E_gc`=⟨H0−μN⟩=E_hub−μ·ntot、`E_ph`=粒子正孔対称形。
> いずれも DQMC の grand-canonical 重み `e^{−β(H0−μN)}`（μ=U/2）での期待値。
> `sign` は v1 の半充填二部格子では絶対符号として 1 に保たれる。non-bipartite は実行エラーにする。
> ED/TPQ と比較する際は、相手の規約（§5・`VALIDATION.md`）に応じて対応する列と**その誤差列**を使う。
> 主比較は `E_hub`（=⟨H0⟩）を grand-canonical ED の ⟨H0⟩ と突き合わせる（同一 μ 重み・同一演算子）。

- [ ] **Step 5: サンプル入力を作成**

Create `input/1d_L4_U0.txt`:

```
lattice=chain
Lx=4
pbc=1
t=-1.0
U=0.0
dtau=0.1
beta_list=0.5,1.0,2.0,4.0,8.0
nwarm=50
nmeas=200
nbin=10
seed=1
```

Create `input/1d_L4_U4.txt`:

```
lattice=chain
Lx=4
pbc=1
t=-1.0
U=4.0
dtau=0.1
beta_list=0.5,1.0,2.0,4.0,6.0,8.0
nwarm=300
nmeas=3000
nbin=30
seed=1
```

- [ ] **Step 6: ビルド・実行・Commit**

Run: `make tests/test_io && ./tests/test_io && make dqmc && ./dqmc input/1d_L4_U0.txt`
Expected: `# ...` ヘッダ＋各 T 行。`sign` 列が全て `1`、`ntot` 列が `≈4`（半充填）。

```bash
git add src/io.h src/io.c src/main.c tests/test_io.c input/1d_L4_U0.txt input/1d_L4_U4.txt
git commit -m "feat(io,main): input parser, driver, temperature scan + sample inputs"
```

---

## Task 12: 統合検証 — U=0 E(T) 一致と符号=1

**Files:**
- Create: `tests/test_integration.c`

- [ ] **Step 1: 失敗するテストを書く**

U=0 を1点 (β=2) 走らせ、QMC の E（=Ekin）が解析値 `2Σ ε_i f(ε_i)` と統計誤差内で一致、かつ sign=1 を確認。U=0 は決定論的（受理に乱数を使うが場が結果に効かない）なので厳密一致する。

Create `tests/test_integration.c`:

```c
#include "test_util.h"
#include "lattice.h"
#include "model.h"
#include "field.h"
#include "dqmc.h"
#include "measure.h"
#include <stdlib.h>
#include <math.h>
extern void dsyev_(const char*,const char*,const int*,double*,const int*,double*,double*,const int*,int*);
int main(void){
    int Lx=4; double dtau=0.1, beta=2.0; int Ltr=20;
    Lattice L; lattice_chain(&L,Lx,-1.0,1);
    Model m; model_init(&m,&L,0.0,dtau,1,0.0);
    Rng r; rng_seed(&r,42);
    Field f; field_init(&f,Lx,Ltr,0.0,dtau,&r);
    Dqmc D; dqmc_init(&D,&m,&f,&r,8);
    for(int w=0;w<20;w++) dqmc_sweep(&D);
    green_from_scratch(&D.Gu,0); green_from_scratch(&D.Gd,0);
    MeasSample s=measure_sample(Lx,L.t,0.0,D.Gu.g,D.Gd.g);
    CHECK_CLOSE(D.sign,1.0,1e-12);
    /* 解析 ekin */
    double Kc[16]; for(int i=0;i<16;i++) Kc[i]=m.K[i];
    double w_[4],work[256]; int n=4,lwork=256,info;
    dsyev_("N","U",&n,Kc,&n,w_,work,&lwork,&info);
    double ek=0; for(int i=0;i<4;i++){ double fe=1.0/(1.0+exp(beta*w_[i])); ek+=w_[i]*fe; } ek*=2.0;
    CHECK_CLOSE(s.E,ek,1e-7);
    dqmc_free(&D); field_free(&f); model_free(&m); lattice_free(&L);
    TEST_END();
}
```

- [ ] **Step 2: テスト確認**

Run: `make tests/test_integration && ./tests/test_integration`
Expected: `OK`

- [ ] **Step 3: 全テスト確認**

Run: `make test`
Expected: `ALL TESTS PASSED`

- [ ] **Step 4: Commit**

```bash
git add tests/test_integration.c
git commit -m "test: end-to-end U=0 energy vs analytic + sign=1"
```

---

## Task 13: U>0 検証手順の文書化（ED/TPQ 照合）

**Files:**
- Create: `VALIDATION.md`

- [ ] **Step 1: 検証手順を書く**

Create `VALIDATION.md`:

```markdown
---
date: YYYY-MM-DD
datetime: YYYY-MM-DD HH:MM JST   # 作成時に date コマンドで確認して記入
model: <使用した生成AIモデル名>
summary: |
  半充填ハバード模型の E(T) を grand-canonical ED/TPQ と比較する検証手順。
  アンサンブル整合・dtau→0 外挿・規約変換・符号/⟨N⟩ チェック・2D の ED サイズ制約をまとめる。
---

# 検証手順: E(T) を ED/TPQ と比較

QMC 出力列: `T  E_hub dE_hub  E_gc dE_gc  E_ph dE_ph  ntot dN  doublon dD  sign`
（`E_hub`=⟨H0⟩=ekin+eint, `E_gc`=⟨H0−μN⟩, `E_ph`=⟨H0−(U/2)N+(U/4)·n_site⟩。H0=K_hop+Un↑n↓）。

## 0. アンサンブルの整合（最重要）
DQMC は μ=U/2 の **grand-canonical** ensemble。粒子正孔対称点で ⟨N⟩=n_site には
なるが、**固定 N=n_site の canonical ensemble と有限温度・有限サイズで一致するとは限らない**
（N≠n_site セクターの寄与が残る）。本プロジェクトの検証対象は 1D L=4-6 / 2D 小系で
有限サイズ効果が大きいため、この差は無視できない。

- **原則: grand-canonical ED と比較する。** μ を**一度だけ**重みに入れること（二重に引かない）。
  `H0 = K_hop + U n↑n↓`（μ を含まない）として、
  `Z = Tr exp[−β(H0 − μN)]`, `μ=U/2`、`⟨O⟩ = Tr[O exp(−β(H0−μN))]/Z`。
  これで `E_hub = ⟨H0⟩`、`E_gc = ⟨H0 − μN⟩ = E_hub − μ⟨N⟩`。
  **主比較**: QMC の `E_hub`（=⟨H0⟩）を grand-canonical ED の `⟨H0⟩` と突き合わせる
  （同一 μ 重み・同一演算子なので最も素直）。`E_gc` 列を使う場合は ED 側も `⟨H0−μN⟩` を返す。
- canonical TPQ/ED（固定 N）を使う場合は、**直接一致を主張せず**、アンサンブル差があり得る
  検証項目として扱う（高温で一致、低温で基底状態に収束すれば概ね一致するが、中間 T で差が出得る）。

## 1. Δτ→0 外挿（Trotter 誤差）
固定 β（例 β=4）で dtau=0.2,0.1,0.05 を `input/1d_L4_U4.txt` の dtau を変えて実行し、
各規約のエネルギーを `dtau^2` で線形外挿する（`E(dtau)=E(0)+c·dtau^2`）。
U=4,8 では統計誤差を小さくすると Trotter 系統誤差が支配的になるため、ED 比較は外挿後に行う。

## 2. ED/TPQ との比較（本命）
- 系: 1D L=4, PBC, U/t=4, 半充填。`E_qmc(dtau→0)` を §1 で得る。
- ED/TPQ で同一 Hamiltonian の E(T) を計算。**規約を揃える**:
  本コードは `U n↑n↓ − μΣn`（μ=U/2）。相手が `U(n↑−½)(n↓−½)` 形なら
  `E_ph = E_hub − (U/2)·ntot + (U/4)·n_site`（半充填で ⟨ntot⟩=n_site なら一定シフト `−U n_site/4`）。
  → QMC は `E_hub`/`E_gc`/`E_ph` を同時出力するので、相手の規約に対応する列を選ぶ。
- **許容差**: §1 の `dtau→0` 外挿後に `|E_qmc(0) − E_ED| < 2·(統計誤差 + 外挿誤差)`。
  外挿前の生データで比較する場合は、判定に Trotter 系統誤差を明示的に加える（統計誤差だけで判定しない）。

## 3. ⟨N⟩ と符号のチェック
- `ntot` 列 ≈ n_site（半充填の前提確認）。
- `sign` 列 = 1.000（半充填・二部格子で符号問題なし）。v1 は non-bipartite を実行エラーにする。

## 4. 2D の注意（ED のサイズ現実性）
- 2×2 PBC は二重結合（`t_ij=−2`）が生じるため定量比較に使わない。
- **grand-canonical ED は Hilbert 空間 4^N と急増する**ため、2日ハッカソンの ED 照合用 2D は
  小系に限る: `2×2 OBC`（4 site, 4^4=256）, `2×3 OBC`, `2×4 OBC/ラダー`（8 site, 4^8=65536）。
  これらは OBC なので二重結合も出ない。
- `4×4`（16 site）は grand-canonical ED には大きすぎる。TPQ や別手法向けの検証候補として分けて扱う。
- 2D 検証では必ず DQMC が dump した hopping 行列（`hopping_used.txt`, Task 14）を ED 側へ渡し、規約を一致させる。
```

- [ ] **Step 2: 実行して動作確認**

Run: `./dqmc input/1d_L4_U4.txt | tee qmc_u4.dat`
Expected: 各行の `sign`≈1、`ntot`≈4、E が低温で基底状態エネルギーに漸近。

- [ ] **Step 3: Commit**

```bash
git add VALIDATION.md
git commit -m "docs: validation procedure for ED/TPQ comparison"
```

---

## Task 14: 任意格子のファイル入力 + hopping 行列 dump（拡張）

設計書 §1 のスコープ「任意格子のファイル入力」を実装する。ED 側と hopping 行列を完全一致
させるため、DQMC が使う行列を dump する機能も併せて入れる。**2日間で時間が無ければ後回し可（stretch）**。

**Files:**
- Modify: `src/lattice.h`, `src/lattice.c`, `src/io.h`, `src/io.c`, `src/main.c`
- Test: `tests/test_lattice_file.c`

- [ ] **Step 1: ヘッダに追加**

Append to `src/lattice.h` (before `#endif`):

```c
/* dense 行列ファイルを読む。形式: 1行目 n、続く n 行に各行 n 個の t_ij（行優先で記述）。
   読み込み後は column-major で格納。対称性を検証し、非ゼロ hopping graph を二色塗りして二部性も判定する。 */
int  lattice_from_file(Lattice *L, const char *path);
/* hopping 行列を dense 形式で書き出す（ED 入力生成・規約一致確認用） */
int  lattice_dump(const Lattice *L, const char *path);
```

- [ ] **Step 2: 失敗するテスト**

Create `tests/test_lattice_file.c` — dump→read で行列が一致すること、diagonal 非ゼロを拒否することを確認:

```c
#include "test_util.h"
#include "lattice.h"
#include <stdio.h>
int main(void){
    Lattice L; lattice_chain(&L,4,-1.0,1);
    const char *path="/tmp/aftest_hop.txt";
    CHECK(lattice_dump(&L,path)==0);
    Lattice F; CHECK(lattice_from_file(&F,path)==0);
    CHECK(F.n==4);
    for(int k=0;k<16;k++) CHECK_CLOSE(F.t[k],L.t[k],1e-12);
    CHECK(F.is_bipartite==1);
    lattice_free(&F); lattice_free(&L);

    const char *bad="/tmp/aftest_diag_hop.txt";
    FILE *fp=fopen(bad,"w");
    fprintf(fp,"2\n0.5 -1\n-1 0\n");
    fclose(fp);
    Lattice B; CHECK(lattice_from_file(&B,bad)!=0);
    TEST_END();
}
```

- [ ] **Step 3: 実装を追加**

Append to `src/lattice.c` (先頭に `#include <stdio.h>` と `#include <math.h>` を追加):

```c
static void detect_bipartite_from_hopping(Lattice *L){
    int n=L->n;
    int *q=malloc(sizeof(int)*n);
    for(int i=0;i<n;i++) L->bipart[i]=0;
    L->is_bipartite=1;
    for(int s=0;s<n && L->is_bipartite;s++){
        if(L->bipart[s]!=0) continue;
        L->bipart[s]=+1;
        int head=0,tail=0; q[tail++]=s;
        while(head<tail && L->is_bipartite){
            int i=q[head++];
            for(int j=0;j<n;j++){
                if(i==j) continue;
                if(fabs(L->t[i+j*n])<=1e-12) continue;
                int want=-L->bipart[i];
                if(L->bipart[j]==0){ L->bipart[j]=want; q[tail++]=j; }
                else if(L->bipart[j]!=want){ L->is_bipartite=0; break; }
            }
        }
    }
    free(q);
    if(!L->is_bipartite) for(int i=0;i<n;i++) L->bipart[i]=0;
}
int lattice_from_file(Lattice *L,const char *path){
    FILE *fp=fopen(path,"r"); if(!fp){ perror(path); return 1; }
    int n; if(fscanf(fp,"%d",&n)!=1){ fclose(fp); return 1; }
    if(n<=0){ fprintf(stderr,"ERROR: hopping matrix size must be positive\n"); fclose(fp); return 1; }
    alloc_lat(L,n);
    for(int i=0;i<n;i++) for(int j=0;j<n;j++){
        double v;
        if(fscanf(fp,"%lf",&v)!=1){
            fclose(fp);
            lattice_free(L);
            return 1;
        }
        L->t[i+j*n]=v;                 /* 行優先入力 → column-major 格納 */
    }
    fclose(fp);
    /* v1 は onsite disorder w_i=0。diagonal hopping は self-bond/onsite potential として扱われ、
       半充填二部格子の sign-free 前提を静かに崩し得るため拒否する。 */
    for(int i=0;i<n;i++){
        if(fabs(L->t[i+i*n])>1e-12){
            fprintf(stderr,"ERROR: hopping matrix diagonal must be zero at (%d,%d)\n",i,i);
            lattice_free(L); return 1;
        }
    }
    /* model_init は la_expm_sym (dsyev) を使うので K は対称必須。
       非対称 t_ij は dsyev が片三角だけ見て誤った行列指数を静かに作るため拒否する。
       （非対称 hopping を扱うには一般行列指数の実装が別途必要） */
    for(int i=0;i<n;i++) for(int j=i+1;j<n;j++){
        if(fabs(L->t[i+j*n]-L->t[j+i*n])>1e-12){
            fprintf(stderr,"ERROR: hopping matrix not symmetric at (%d,%d)\n",i,j);
            lattice_free(L); return 1;
        }
    }
    detect_bipartite_from_hopping(L);
    return 0;
}
int lattice_dump(const Lattice *L,const char *path){
    FILE *fp=fopen(path,"w"); if(!fp){ perror(path); return 1; }
    int n=L->n; fprintf(fp,"%d\n",n);
    for(int i=0;i<n;i++){ for(int j=0;j<n;j++) fprintf(fp,"%.17g ",L->t[i+j*n]); fprintf(fp,"\n"); }
    fclose(fp);
    return 0;
}
```

- [ ] **Step 4: io/main で `lattice=file` と `latfile=...` を扱う**

Modify `src/io.h` の `Params` に `char latfile[256];` を追加し、`lattice` コメントを
`"chain" | "square" | "file"` に更新する。`defaults` で `p->latfile[0]=0;` を設定し、
パーサの unknown-key エラーより前に次を追加する（`strncpy` 後に必ず終端する）:

```c
else if(!strcmp(key,"latfile")){
    strncpy(p->latfile,val,sizeof p->latfile - 1);
    p->latfile[sizeof p->latfile - 1]='\0';
}
```

Task 11 で追加した `lattice` 許可値検証は、`file` も受理するように更新する:

```c
if(strcmp(p->lattice,"chain") && strcmp(p->lattice,"square") && strcmp(p->lattice,"file")){
    fprintf(stderr,"ERROR: lattice must be chain, square, or file (got %s)\n",p->lattice);
    return 1;
}
```

`src/main.c` の格子生成を次に置換:

```c
    Lattice L;
    if(!strcmp(p.lattice,"file")){
        if(p.latfile[0]=='\0'){ fprintf(stderr,"ERROR: lattice=file requires latfile\n"); return 1; }
        if(lattice_from_file(&L,p.latfile)){ fprintf(stderr,"failed to read latfile\n"); return 1; }
    } else if(!strcmp(p.lattice,"square")) lattice_square(&L,p.Lx,p.Ly,p.thop,p.pbc);
    else if(!strcmp(p.lattice,"chain"))    lattice_chain(&L,p.Lx,p.thop,p.pbc);
    else { fprintf(stderr,"ERROR: unknown lattice %s\n",p.lattice); return 1; }
    lattice_dump(&L,"hopping_used.txt");   /* ED 規約一致確認用に常に dump */
```

- [ ] **Step 5: テスト・Commit**

Run: `make tests/test_lattice_file && ./tests/test_lattice_file`
Expected: `OK`

```bash
git add src/lattice.h src/lattice.c src/io.h src/io.c src/main.c tests/test_lattice_file.c
git commit -m "feat(lattice): arbitrary hopping-matrix file input + dump"
```

---

## Task 15: 強化テスト — B_l の列スケール（column-major 取り違え防止）

`green_build_B` は `B_lσ = expK · diag(exp(λσ s − ΔτU/2))`、すなわち expK の **列 j** を係数倍する。
column-major と右掛け対角行列の取り違えが起きやすい箇所なので、手計算可能な小系で明示テストする。

**Files:**
- Test: `tests/test_green_buildb.c`

- [ ] **Step 1: 失敗するテスト**

Create `tests/test_green_buildb.c`:

```c
#include "test_util.h"
#include "lattice.h"
#include "model.h"
#include "field.h"
#include "green.h"
#include <math.h>
int main(void){
    int Lx=2, Ltr=4; double dtau=0.1, U=4.0;
    Lattice L; lattice_chain(&L,Lx,-1.0,0);     /* OBC 2-site */
    Model m; model_init(&m,&L,U,dtau,1,0.0);
    Rng r; rng_seed(&r,9);
    Field f; field_init(&f,Lx,Ltr,U,dtau,&r);
    Green G; green_alloc(&G,&m,&f,+1.0);
    double B[4]; green_build_B(&G,0,B);
    /* 期待: B[i+j*n] = expK[i+j*n] * d_j, d_j = exp(lambda*sigma*s_{0j} - dtau*U/2) */
    double c=-dtau*U/2.0;
    for(int j=0;j<Lx;j++){
        double dj=exp(f.lambda*(+1.0)*(double)f.s[0*Lx+j]+c);
        for(int i=0;i<Lx;i++) CHECK_CLOSE(B[i+j*Lx], m.expK[i+j*Lx]*dj, 1e-12);
    }
    green_free(&G); field_free(&f); model_free(&m); lattice_free(&L);
    TEST_END();
}
```

- [ ] **Step 2: テスト確認**

Run: `make tests/test_green_buildb && ./tests/test_green_buildb`
Expected: `OK`（既存 `green_build_B` がそのまま通る。通らなければ列/行スケールの取り違えを修正）

- [ ] **Step 3: Commit**

```bash
git add tests/test_green_buildb.c
git commit -m "test(green): explicit B_l column-scaling check"
```

---

## Self-Review（計画作成者による確認）

- **Spec coverage**: 設計書 §3 モジュール → Task 1–11 で実装、任意格子ファイル入力は Task 14。§5 測定式・規約変換 → Task 9（素片）＋Task 11（規約別エネルギー出力）。§6 検証 (1)U=0→Task6,10,12 (2)Δτ外挿→Task13 (3)安定化残差→Task7,8 (4)ED/TPQ照合→Task13 (5)符号→Task12,13。§8 ビルド/IO→Task0,11。
- **Placeholder scan**: 各コード step に実コードあり。`green.c` の lmul 巡回順、`udv_inv_one_plus` の未使用 `DbU` は明示の整理ステップ付き。
- **Type consistency**: `Green`/`Model`/`Field`/`Dqmc`/`Lattice`/`MeasSample` のフィールド名、`green_flipN/green_ratio_N/green_update/green_wrap/green_from_scratch/green_build_B`、`measure_sample` のシグネチャは全タスクで一貫。column-major 規約は全 `[i+j*n]` で統一。
- **既知の注意点**: UDV の安定化は小サイズ・中 β でまず検証。大 β で残差が悪化する場合は `stab_interval` を小さくする（Task10 のスケジュールで対応可能）。

## レビュー反映履歴（2026-06-25, Codex レビュー `docs/reviews/2026-06-25-...plan-review.md` 対応）

1. **[Critical] 補助場 flip の N 符号**: `N` を反転前の場から1回だけ計算する `green_flipN` を導入し、
   受理比 `green_ratio_N` と更新 `green_update(…,N)` に明示的に渡す API へ変更。pre/post 取り違えを
   構造的に排除（Task 7・Task 10）。式 A.91–A.93 から `N=exp(−2λσ s_old)−1`（s_old=反転前）を再確認。
2. **[High] ED/TPQ アンサンブル**: grand-canonical ED を原則と明記し、canonical との差を検証項目化（Task 13 §0）。
3. **[High] エネルギー定義・出力**: `MeasSample` に `ntot` 追加、main で `E_hub/E_gc/E_ph` を同時出力（Task 9・11）。
4. **[High] Trotter 誤差**: ED 比較は `dtau²` 外挿後、許容差に系統誤差を加える（Task 13 §1–2）。
5. **[Medium] 二部性**: `Lattice.is_bipartite` を追加・判定し、非二部半充填を警告（Task 3・11）。
6. **[Medium] 2×2 PBC 二重結合**: 定量比較から除外、hopping dump で ED と規約一致（Task 3 注・13 §4・14）。
7. **[Medium] 任意格子ファイル入力**: Task 14 として追加（dump 機能込み）。
8. **[Medium] beta/dtau 丸め**: 非整数はエラー、出力は `beta_eff=Ltr·dtau` を使用（Task 11）。
9. **[Low] git add パス**: 全コマンドをリポジトリルート相対に修正（規約に作業ディレクトリ明記）。
10. **[Low] main.c include**: `#include <string.h>` をコード本体に追加（Task 11）。
11. **追加テスト**: B_l 列スケール（Task 15）、⟨N⟩ 半充填（Task 9 測定テスト）、二部性（Task 3）、両スピン更新（Task 7）。

### 再レビュー対応（2026-06-25, `docs/reviews/2026-06-25-...plan-rereview.md`）

12. **[High] grand-canonical の μ 二重カウント**: VALIDATION の `Tr[O e^{−β(H−μN)}]`（H が既に −μN を含む）を
    `H0=K_hop+Un↑n↓`・`Tr exp[−β(H0−μN)]`・`E_gc=⟨H0−μN⟩` に統一（Task 13 §0）。主比較は `E_hub=⟨H0⟩`。
13. **[High] E_gc/E_ph の誤差出力**: 出力列を `E_hub dE_hub E_gc dE_gc E_ph dE_ph …` に拡張し printf も更新（Task 11）。
14. **[Medium] 2D ED の現実性**: grand-canonical ED は 4^N と急増するため照合用 2D を小 OBC 系（2×2/2×3/2×4）に限定、
    4×4 は TPQ 向けに分離（Task 13 §4・設計書 §6）。
15. **[Medium] 任意格子の対称性検証**: `lattice_from_file` で非対称 hopping を拒否（`la_expm_sym`=dsyev 前提のため, Task 14）。
16. **[Medium] down-spin テスト**: Task 7 のテストを `sigma=±1` のループに変更し両スピンを実コードで検証。
17. **[Low] jackknife N=1 ゼロ割り**: `params_read` で `nbin≥2` と `nmeas≥nbin` を検証、`jackknife` も `N<2` を防御（Task 9・11）。
18. **[Low] docs frontmatter**: AGENTS.md ルールに従い設計書・計画書の冒頭に frontmatter を追加。

### 第3回レビュー対応（2026-06-25, `docs/reviews/2026-06-25-...plan-third-review.md`）

物理・数値バグは指摘なし。文書整合性の修正のみ:

19. **[High] VALIDATION の古い出力列**: Task 13 テンプレートの列を新 12 列 `…E_hub dE_hub E_gc dE_gc E_ph dE_ph…` に更新し、`E_gc=⟨H0−μN⟩` 表記に統一。
20. **[Medium] 設計書 §8 の古い出力例**: `T E dE doublon sign` → 新 12 列に更新。§7 も各規約の `E±δE` に。
21. **[Medium] 入力 key 名の不一致**: 設計書を `pbc`/`stab` に寄せ、パーサに `bc`(periodic/open)・`stabilize_interval` の alias を追加（Task 11・設計書 §8）。
22. **[Medium] square の縮退サイズ self-bond**: `lattice_square`/`lattice_chain` の PBC wrap を `Lx>1`/`Ly>1` 条件付きにし、Ly==1 等の self-bond を回避。テストも追加（Task 3）。
23. **[Low] VALIDATION.md の frontmatter**: Task 13 テンプレート冒頭に frontmatter を追加。

### 第4回レビュー対応（2026-06-25, `docs/reviews/2026-06-25-...plan-fourth-review.md`）

24. **[Medium] H0/E_gc 表記の曖昧さ**: 設計書 §5 を `H0=K_hop+UΣn↑n↓`、DQMC 重み `exp[-β(H0−μN)]`、`E_gc=⟨H0−μN⟩` に統一。
25. **[Medium] 縮退サイズの二部性**: `Lx==1`/`Ly==1` の PBC wrap は self-bond を作らないため、二部性判定でも `L>1` の実 bond 方向だけを奇数判定に使う（Task 3）。
26. **[Low] non-bipartite sign**: v1 は non-bipartite を実行エラーにし、`sign` 列を半充填二部格子の絶対符号として限定。将来対応時は初期 determinant sign 計算が必要と明記。
27. **[Low] `strncpy` 終端保証**: `lattice` と `latfile` のコピー後に明示的な `'\0'` 終端を追加（Task 11・14）。
28. **[Low] `bc` alias typo 検出**: `bc` は `periodic/open/1/0`（加えて `pbc/obc`）のみ受理し、それ以外はエラーにする（Task 11）。

### 第5回レビュー確認対応（2026-06-25, `docs/reviews/2026-06-25-...plan-sixth-review.md`）

29. **[Medium] `lattice` key typo 検出**: `lattice=sqaure` 等が chain に silent fallback しないよう、Task 11 で `chain|square` を許可値検証し、Task 14 で `file` を追加。main も防御的に未知 lattice をエラーにする。
30. **[Medium] file hopping の diagonal 拒否**: v1 は onsite disorder `w_i=0` のため、`lattice_from_file` で `t_ii != 0` を拒否し、diagonal 非ゼロファイルの失敗テストを追加。

### 第7回レビュー対応（2026-06-25, `docs/reviews/2026-06-25-...plan-seventh-review.md`）

31. **[Medium] `green_from_scratch` 非可換積テスト**: Task 6 の `tests/test_green_init.c` に U>0 の小系で `B_l` を明示積し、`l0=0/1` の `green_from_scratch` と比較するテストを追加。
32. **[Medium] 入力 parser 厳格化**: Task 11 で未知 key をエラーにし、`strtol`/`strtod`/`strtoull` による数値検証、範囲検証、`beta_list` overflow 検出、`nmeas%nbin==0` 検証を追加。`tests/test_io.c` も追加。
33. **[Medium] UDV stress test**: Task 2 の `tests/test_udv.c` に強いスケール分離を含む residual test `(I+P)g≈I` を追加。
34. **[Medium] 2D 検証記述**: Task 3 の注を小 OBC 系（`2×2/2×3/2×4 OBC`）へ統一し、`4×4` は TPQ・将来検証候補として明記。
35. **[Low] file 入力失敗経路 cleanup**: Task 14 の `lattice_from_file` で malformed file と `n<=0` をエラーにし、確保後の読み込み失敗時は `lattice_free(L)` して返るように変更。
36. **[Low] `nmeas%nbin` の扱い**: Task 11 の入力検証で `nmeas` が `nbin` で割り切れることを要求し、余り測定を黙って捨てない仕様に変更。
