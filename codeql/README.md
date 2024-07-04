To scan the Linux kernel with CodeQL, you'll need to download CodeQL itself. There is lots of documentation about how to use it with [VS Code](https://docs.github.com/en/code-security/codeql-for-vs-code/getting-started-with-codeql-for-vs-code/about-codeql-for-vs-code), but this guide is focused on using it exclusively from the [command-line](https://docs.github.com/en/code-security/codeql-cli/codeql-cli-manual/).

Once downloaded, you'll need to generate the database with a kernel build:

```
cd linux
../codeql/codeql database create ../db --language c --command 'make -j128 allmodconfig all -s'
```

Then you can run queries:

```
export CODEQL=/path/to/kernel-tools/codeql
codeql/codeql query run --database=/srv/code/codeql/db --threads=128 --common-caches=/srv/code/codeql/.codeql "$CODEQL"/some-query.ql'
```

My docker image's home directory isn't writable, so I had to redirect the cache somewhere else with the `--common-caches` argument.

To construct a query, CodeQL uses database query language that looks like SQL. The available "tables" that can be queries, and their inter-relationships, is documented [here](https://codeql.github.com/docs/codeql-language-guides/codeql-for-cpp/)

For the shipped C examples, see `codeql/cpp-examples/*/...`

If you get an especially huge query, you may need to tweak Java's RAM usage with `--ram=102400` (e.g. 10G)

Another example of setup and searching:

 - https://zetier.com/codeql-for-security-research/

And notes on how to keep query time low by keeping variables scoped correctly:

 - https://github.com/github/codeql/discussions/16903
