# semaphore-process-manager
A C-based multi-process synchronization simulator utilizing POSIX semaphores and shared memory via mmap. The application uses fork() to create multiple concurrent child processes (representing a dispatcher, vehicles, and passengers) and safely coordinates their interactions and shared state using a series of custom semaphores.
