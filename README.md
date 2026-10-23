# VyricSync

Visual Studio環境から仮想マシン（VM）への同期・連携を行うためのツールです。

## 概要

このプロジェクトは、ローカルの開発環境（Visual Studio）で書き換えたソースコードやビルド成果物を、ターゲットとなる仮想マシン（VM）環境へ効率的に同期し、実行・デバッグをスムーズに行うことを目的としています。

## 特徴

- **自動同期:** ファイルの変更を検知して自動的にVMへ転送します。
- **簡単設定:** 接続先のIPアドレスや認証情報を指定するだけで利用可能です。
- **軽量動作:** バックグラウンドで最小限のリソースで動作します。

## 動作環境

- OS: Windows 10 / 11
- 開発環境: Visual Studio 2022
- 同期先: Linux (Ubuntu / CentOS) または Windows VM

## 使い方（クイックスタート）

1. このリポジトリをクローンします。
   ```bash
   git clone https://github.com
   ```
2. 設定ファイル（例: `config.json`）を開き、同期先のVM情報を入力します。
3. アプリケーションを実行します。

## ライセンス

[MIT License](LICENSE)
Copyright (c) [year] [fullname]

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
