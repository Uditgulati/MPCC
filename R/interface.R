# copyright (c) - HU-Berlin / UTHSC / JICS by Danny Arends

# PCC matrix c wrapper
PCC <- function(aM, bM = NULL, use = NULL, asMatrix = TRUE, debugOn = FALSE) {
  if(is.null(bM)) bM <- aM
  res <- .C("R_pcc_matrix", aM = as.double(aM),
                            bM = as.double(bM),
                            n = as.integer(nrow(aM)), # nInd
                            m = as.integer(ncol(aM)), # nPhe A
                            p = as.integer(ncol(bM)), # nPhe B
                            res = as.double(rep(0, ncol(aM) * ncol(bM))), NAOK = TRUE, package = "MPCC")

  if(asMatrix) res$res <- matrix(res$res, ncol(aM), ncol(bM), byrow=TRUE, dimnames = list(colnames(aM), colnames(bM)))
  if(debugOn) return(res)
  return(res$res)
}

# PCC naive c wrapper
PCC.naive <- function(aM, bM = NULL, use = NULL, asMatrix = TRUE, debugOn = FALSE) {
  if(is.null(bM)) bM <- aM
  res <- .C("R_pcc_naive", aM = as.double(aM),
                           bM = as.double(bM),
                           n = as.integer(nrow(aM)), # nInd
                           m = as.integer(ncol(aM)), # nPhe A
                           p = as.integer(ncol(bM)), # nPhe B
                           res = as.double(rep(0, ncol(aM) * ncol(bM))), NAOK = TRUE, package = "MPCC")

  if(asMatrix) res$res <- matrix(res$res, ncol(aM), ncol(bM), byrow=TRUE, dimnames = list(colnames(aM), colnames(bM)))
  if(debugOn) return(res)
  return(res$res)
}

