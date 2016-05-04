#!/usr/bin/env python

#Copyright (C) 2011 by Glenn Hickey
#
#Released under the MIT license, see LICENSE.txt
#!/usr/bin/env python

"""Wrapper to run the cactus_workflow progressively, using the input species tree as a guide

tree.  
"""

import os
import xml.etree.ElementTree as ET
import math
from argparse import ArgumentParser
from collections import deque
import random
from itertools import izip
from shutil import move
import copy
from time import sleep

from sonLib.bioio import getTempFile
from sonLib.bioio import printBinaryTree
from sonLib.bioio import system

from toil.lib.bioio import getLogLevelString
from toil.lib.bioio import logger
from toil.lib.bioio import setLoggingFromOptions

from cactus.shared.common import cactusRootPath
from cactus.shared.common import getOptionalAttrib
  
from toil.job import Job
from toil.common import Toil

from cactus.preprocessor.cactus_preprocessor import CactusPreprocessor
from cactus.pipeline.cactus_workflow import CactusWorkflowArguments
from cactus.pipeline.cactus_workflow import addCactusWorkflowOptions
from cactus.pipeline.cactus_workflow import findRequiredNode
from cactus.pipeline.cactus_workflow import CactusSetupPhase
from cactus.pipeline.cactus_workflow import CactusTrimmingBlastPhase

from cactus.progressive.multiCactusProject import MultiCactusProject
from cactus.progressive.multiCactusTree import MultiCactusTree
from cactus.shared.experimentWrapper import ExperimentWrapper
from cactus.shared.configWrapper import ConfigWrapper
from cactus.progressive.schedule import Schedule


class ProgressiveDown(Job):
    def __init__(self, options, project, event, schedule, parentExpWrapper = None, parentName = None):
        Job.__init__(self)
        self.options = options
        self.project = project
        self.event = event
        self.schedule = schedule
        self.parentExpWrapper = parentExpWrapper
        self.parentName = parentName
    
    def run(self, fileStore):
        #project = MultiCactusProject()
        #project.readXML(fileStore.readGlobalFile(self.projectID))
        if self.parentExpWrapper:
            self.project.outputSequenceIDMap[self.parentName] = self.parentExpWrapper.getReferenceID()
            self.project.expIDMap[self.parentName] = self.parentExpID
            
        logger.info("Progressive Down: " + self.event)
        #self.projectID = project.writeXMLToFileStore(fileStore)

        depProjects = dict()
        if not self.options.nonRecursive:
            deps = self.schedule.deps(self.event)
            for child in deps:
                depProjects[child] = self.addChild(ProgressiveDown(self.options,
                                                    self.project, child, 
                                                                   self.schedule)).rv()
        
        return self.addFollowOn(ProgressiveNext(self.options, self.project, self.event,
                                                              self.schedule, depProjects)).rv()
class ProgressiveNext(Job):
    def __init__(self, options, project, event, schedule, depProjects):
        Job.__init__(self)
        self.options = options
        self.project = project
        self.event = event
        self.schedule = schedule
        self.depProjects = depProjects
    
    def run(self, fileStore):
        #project = MultiCactusProject()
        #project.readXML(fileStore.readGlobalFile(self.projectID))
        for projName in self.depProjects:
            #depProject = MultiCactusProject()
            #depProject.readXML(fileStore.readGlobalFile(self.depProjectIDs[name]))
            depProject = self.depProjects[projName]
            for expName in depProject.expIDMap: 
                expID = depProject.expIDMap[expName]
                logger.info("ExpID %s" % expID)
                experiment = ExperimentWrapper(ET.parse(fileStore.readGlobalFile(expID)).getroot())
                logger.info("Reference id: %s, reference path: %s" % (experiment.getReferenceID(), experiment.getReferencePath()))
                logger.info("Getting the reference sequence for %s" % expName)
                if experiment.getReferenceID():
                    self.project.expIDMap[expName] = expID
                    self.project.outputSequenceIDMap[expName] = experiment.getReferenceID()
        #self.projectID = project.writeXMLToFileStore(fileStore)
                        
        eventExpWrapper = None
        logger.info("Progressive Next: " + self.event)
        if not self.schedule.isVirtual(self.event):
            eventExpWrapper = self.addChild(ProgressiveUp(self.options, self.project, self.event)).rv()
            #self.project.expIDMap[self.event] = eventExpID
        return self.addFollowOn(ProgressiveOut(self.options, self.project, self.event, eventExpWrapper, self.schedule)).rv()

class ProgressiveOut(Job):
    def __init__(self, options, project, event, eventExpWrapper, schedule):
        Job.__init__(self)
        self.options = options
        self.project = project
        self.event = event
        self.eventExpWrapper = eventExpWrapper
        self.schedule = schedule
        
    def run(self, fileStore):
        #project = MultiCactusProject()
        #project.readXML(fileStore.readGlobalFile(self.projectID))
        tmpExp = os.path.join(fileStore.getLocalTempDir(), "tmpExp")
        self.eventExpWrapper.writeXML(tmpExp)
        self.project.expIDMap[self.event] = fileStore.writeGlobalFile(tmpExp)
        #return project.writeXMLToFileStore(fileStore)
        followOnEvent = self.schedule.followOn(self.event)
        if followOnEvent is not None:
            logger.info("Adding follow-on event %s" % followOnEvent)
            return self.addFollowOn(ProgressiveDown(self.options, self.project, followOnEvent,
                                                    self.schedule, parentExpWrapper = eventExpWrapper, parentName = self.event)).rv()

        return self.project
    
class ProgressiveUp(Job):
    def __init__(self, options, project, event):
        Job.__init__(self)
        self.options = options
        self.project = project
        self.event = event
    
    def run(self, fileStore):
        logger.info("Progressive Up: " + self.event)

        #project = MultiCactusProject()
        #project.readXML(fileStore.readGlobalFile(self.projectID))

        # open up the experiment
        # note that we copy the path into the options here
        experimentFile = fileStore.readGlobalFile(self.project.expIDMap[self.event])
        expXml = ET.parse(experimentFile).getroot()
        experiment = ExperimentWrapper(expXml)
        configXml = ET.parse(experiment.getConfigPath()).getroot()
        configWrapper = ConfigWrapper(configXml)

        seqMap = experiment.buildSequenceMap()
        experimentSeqIDs = []
        for name in seqMap:
            experimentSeqIDs.append(self.project.outputSequenceIDMap[name])
        experiment.setSequenceIDs(experimentSeqIDs)
            
        newExpFilePath = os.path.join(fileStore.getLocalTempDir(), "expTemp")
        experiment.writeXML(newExpFilePath)
        self.options.experimentFileID = fileStore.writeGlobalFile(newExpFilePath)

        # need at least 3 processes for every event when using ktserver:
        # 1 proc to run jobs, 1 proc to run server, 1 proc to run 2ndary server
        if experiment.getDbType() == "kyoto_tycoon":            
            maxParallel = min(len(self.project.expMap),
                             configWrapper.getMaxParallelSubtrees()) 
            if self.options.batchSystem == "singleMachine":
                pass
                #if int(self.options.maxThreads) < maxParallel * 3:
                    #raise RuntimeError("At least %d threads are required (only %d were specified) to handle up to %d events using kyoto tycoon. Either increase the number of threads using the --maxThreads option or decrease the number of parallel jobs (currently %d) by adjusting max_parallel_subtrees in the config file" % (maxParallel * 3, self.options.maxThreads, maxParallel, configWrapper.getMaxParallelSubtrees()))
            else:
                pass
                #if int(self.options.maxCores) < maxParallel * 3:
                    #raise RuntimeError("At least %d concurrent cpus are required to handle up to %d events using kyoto tycoon. Either increase the number of cpus using the --maxCpus option or decrease the number of parallel jobs (currently %d) by adjusting max_parallel_subtrees in the config file" % (maxParallel * 3, maxParallel, configWrapper.getMaxParallelSubtrees()))
                    
        # take union of command line options and config options for hal and reference
        if self.options.buildReference == False:
            refNode = findRequiredNode(configXml, "reference")
            self.options.buildReference = getOptionalAttrib(refNode, "buildReference", bool, False)
        halNode = findRequiredNode(configXml, "hal")
        if self.options.buildHal == False:
            self.options.buildHal = getOptionalAttrib(halNode, "buildHal", bool, False)
        if self.options.buildFasta == False:
            self.options.buildFasta = getOptionalAttrib(halNode, "buildFasta", bool, False)

        # get parameters that cactus_workflow stuff wants
        experimentFile = fileStore.readGlobalFile(self.options.experimentFileID)
        workFlowArgs = CactusWorkflowArguments(self.options, experimentFile)
        # copy over the options so we don't trail them around
        workFlowArgs.buildReference = self.options.buildReference
        workFlowArgs.buildHal = self.options.buildHal
        workFlowArgs.buildFasta = self.options.buildFasta
        workFlowArgs.overwrite = self.options.overwrite
        workFlowArgs.globalLeafEventSet = self.options.globalLeafEventSet
        
        experiment = ExperimentWrapper(workFlowArgs.experimentNode)

        donePath = os.path.join(os.path.dirname(workFlowArgs.experimentFile), "DONE")
        doneDone = os.path.isfile(donePath)
        refDone = not workFlowArgs.buildReference or os.path.isfile(experiment.getReferencePath())
        halDone = not workFlowArgs.buildHal or (os.path.isfile(experiment.getHALFastaPath()) and
                                                os.path.isfile(experiment.getHALPath()))
        finalExpID = None
                                                               
        if not workFlowArgs.overwrite and doneDone and refDone and halDone:
            self.logToMaster("Skipping %s because it is already done and overwrite is disabled" %
                             self.event)
        else:
            system("rm -f %s" % donePath)
            # delete database 
            # and overwrite specified (or if reference not present)
            dbPath = os.path.join(experiment.getDbDir(), 
                                  experiment.getDbName())
            seqPath = os.path.join(experiment.getDbDir(), "sequences")
            system("rm -f %s* %s %s" % (dbPath, seqPath, 
                                        experiment.getReferencePath()))

            if workFlowArgs.configWrapper.getDoTrimStrategy() and workFlowArgs.outgroupEventNames is not None:
                # Use the trimming strategy to blast ingroups vs outgroups.
                finalExpWrapper = self.addChild(CactusTrimmingBlastPhase(cactusWorkflowArguments=workFlowArgs, phaseName="trimBlast")).rv()
            else:
                finalExpWrapper = self.addChild(CactusSetupPhase(cactusWorkflowArguments=workFlowArgs,
                                                     phaseName="setup")).rv()
        logger.info("Going to create alignments and define the cactus tree")

        #self.addFollowOn(FinishUp(workFlowArgs))
        return finalExpWrapper
                               
class FinishUp(Job):
    def __init__(self, workFlowArgs):
        Job.__init__(self, memory = 100)
        self.workFlowArgs = workFlowArgs
    
    def run(self, fileStore):
        donePath = os.path.join(os.path.dirname(self.workFlowArgs.experimentFile), "DONE")
        doneFile = open(donePath, "w")
        doneFile.write("")
        doneFile.close()

class RunCactusPreprocessorThenProgressiveDown(Job):
    def __init__(self, options, project):
        Job.__init__(self)
        self.options = options
        self.project = project
        
    def run(self, fileStore):
        #Load the multi-cactus project
        #project = MultiCactusProject()
        #projectPath = fileStore.readGlobalFile(self.projectID)
        #project.readXML(projectPath)
        #Create jobs to create the output sequences
        logger.info("Reading config file from: %s" % self.project.getConfigID())
        configFile = fileStore.readGlobalFile(self.project.getConfigID())
        configNode = ET.parse(configFile).getroot()
        ConfigWrapper(configNode).substituteAllPredefinedConstantsWithLiterals() #This is necessary..
        #Add the preprocessor child job. The output is a job promise value that will be
        #converted into a list of the IDs of the preprocessed sequences in the follow on job.
        preprocessorOutput = self.addChild(CactusPreprocessor(self.project.getInputSequenceIDs(), configNode)).rv()

        #Now build the progressive-down job
        schedule = Schedule()
        schedule.loadProject(self.project)
        schedule.compute()
        if self.options.event == None:
            self.options.event = self.project.mcTree.getRootName()
        assert self.options.event in self.project.expMap
        leafNames = [ self.project.mcTree.getName(i) for i in self.project.mcTree.getLeaves() ]
        self.options.globalLeafEventSet = set(leafNames)
        return self.addFollowOn(ProgressiveDownPrecursor(self.options, self.project, preprocessorOutput, self.options.event, schedule)).rv()

class ProgressiveDownPrecursor(Job):
    def __init__(self, options, project, preprocessorOutput, event, schedule):
        Job.__init__(self)
        self.options = options
        self.project = project
        self.preprocessorOutput = preprocessorOutput
        self.event = event
        self.schedule = schedule
    def run(self, fileStore):
        self.project.setOutputSequenceIDs(self.preprocessorOutput)
        outputSequencePaths = CactusPreprocessor.getOutputSequenceFiles(self.project.getInputSequencePaths(), self.project.getOutputSequenceDir())
        self.project.preprocessedSequenceIDs = dict(zip(outputSequencePaths, self.preprocessorOutput))
        #projectID = self.project.writeXMLToFileStore(fileStore)
        return self.addFollowOn(ProgressiveDown(self.options, self.project, self.event, self.schedule)).rv()

def makeURL(path):
    return "file://" + path

        
def main():
    usage = "usage: prog [options] <multicactus project>"
    description = "Progressive version of cactus_workflow"
    parser = ArgumentParser(usage=usage, description=description)
    Job.Runner.addToilOptions(parser)
    addCactusWorkflowOptions(parser)
    
    parser.add_argument("--nonRecursive", dest="nonRecursive", action="store_true",
                      help="Only process given event (not children) [default=False]", 
                      default=False)
    
    parser.add_argument("--event", dest="event", 
                      help="Target event to process [default=root]", default=None)
    
    parser.add_argument("--overwrite", dest="overwrite", action="store_true",
                      help="Recompute and overwrite output files if they exist [default=False]",
                      default=False)
    parser.add_argument("--project", dest="project", help="Directory of multicactus project.")
    
    options = parser.parse_args()
    setLoggingFromOptions(options)
    project = MultiCactusProject()

    with Toil(options) as toil:
        project.readXML(options.project, toil.jobStore)
        #import the sequences
        seqIDs = []
        for seq in project.getInputSequencePaths():
            seqFileURL = makeURL(seq)
            seqIDs.append(toil.jobStore.importFile(seqFileURL))
        project.setInputSequenceIDs(seqIDs)

        
        #import cactus config
        cactusConfigID = toil.jobStore.importFile(makeURL(project.getConfigPath()))
        logger.info("Setting config id to: %s" % cactusConfigID)
        project.setConfigID(cactusConfigID)

        project.writeXML(options.project)

        #import the project file
        #projectID = toil.jobStore.importFile(makeURL(options.project))
        #Run the workflow
        project = toil.run(RunCactusPreprocessorThenProgressiveDown(options, project))

        #Write the HAL file and reference sequence for each experiment wrapper to a permanent
        #path on the leader node
        for name in project.expIDMap:
            toil.jobStore.exportFile(project.expIDMap[name], makeURL(project.expMap[name]))
            expWrapper = ExperimentWrapper(ET.parse(project.expMap[name]).getroot())
            toil.jobStore.exportFile(expWrapper.getHalID(), makeURL(expWrapper.getHALPath()))
            toil.jobStore.exportFile(expWrapper.getReferenceID(), makeURL(expWrapper.getReferencePath()))
            toil.jobStore.exportFile(expWrapper.getHalFastaID(), makeURL(expWrapper.getHALFastaPath()))

        for outputSeqPath in project.preprocessedSequenceIDs:
            toil.jobStore.exportFile(project.preprocessedSequenceIDs[outputSeqPath], makeURL(outputSeqPath))
        #Write the project file to its expected location
        project.writeXML(options.project)



if __name__ == '__main__':
    from cactus.progressive.cactus_progressive import *
    main()
