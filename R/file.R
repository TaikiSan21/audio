load.wave <- function(where, from=0, to=NA_real_, header=FALSE) {
    from <- as.numeric(from)
    to <- as.numeric(to)
    result <- .Call(load_wave_file, where, from, to, as.integer(header), PACKAGE="audio")
    if(header) {
        names(result) <- c('sample.rate', 'channels', 'bits', 'samples')
    }
    result
}

save.wave <- function(what, where) invisible(.Call(save_wave_file, where, what, PACKAGE="audio"))

