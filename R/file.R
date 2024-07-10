load.wave <- function(where, from=0, to=NA_real_) {
    from <- as.numeric(from)
    to <- as.numeric(to)
    invisible(.Call(load_wave_file, where, from, to, PACKAGE="audio"))
}

save.wave <- function(what, where) invisible(.Call(save_wave_file, where, what, PACKAGE="audio"))

