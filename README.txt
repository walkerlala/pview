The pview binary use the compile_commands.json database to index all source
code of a project and then answer several questions:
  1. all callers of a function
  2. all callee of a function
  3. the callgraph between 2 functions if exist
    Note, have to take care of
      - virtual function
      - function pointer
      - think of throwing exception as a function
      - c++ constructor
      - spawning thread/task and things are done in child thread/task
  4. variable modified at which place

Use `./pview --help` to see how to use.
