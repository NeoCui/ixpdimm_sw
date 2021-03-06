/*
 * Copyright (c) 2015 2016, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Intel Corporation nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This file contains the hooks into NVMCLI namespace related commands, and the
 * implementations of miscellaneous commands.
 */

#include <vector>
#include <string>
#include <cstring>

#include <LogEnterExit.h>
#include <mem_config/MemoryConfigurationServiceFactory.h>
#include <mem_config/InterleaveSet.h>
#include <pmem_config/NamespaceViewFactory.h>

#include <libinvm-cli/SimpleResult.h>
#include <libinvm-cli/SimpleListResult.h>
#include <libinvm-cli/CommandSpec.h>
#include <libinvm-cli/PropertyListResult.h>
#include <libinvm-cli/NotImplementedErrorResult.h>
#include <libinvm-cli/OutputOptions.h>

#include "CommandParts.h"
#include "NamespaceFeature.h"
#include <cr_i18n.h>
#include <exception/NvmExceptionLibError.h>
#include "FieldSupportFeature.h"
#include "CreateGoalCommand.h"
#include "ShowGoalCommand.h"

const std::string cli::nvmcli::NamespaceFeature::Name = "Namespace";

/*
 * Command Specs the Namespace Feature supports
 */
void cli::nvmcli::NamespaceFeature::getPaths(cli::framework::CommandSpecList &list)
{
	cli::framework::CommandSpec deleteConfigGoal(DELETE_CONFIG_GOAL,
			TR("Delete Memory Allocation Goal"), framework::VERB_DELETE,
			TR("Delete the memory allocation goal from one or more " NVM_DIMM_NAME "s."));
	deleteConfigGoal.addTarget(TARGET_DIMM.name, false, DIMMIDS_STR, true,
			TR("Delete the memory allocation goal from specific " NVM_DIMM_NAME "s by supplying one or more "
					"comma separated " NVM_DIMM_NAME " identifiers. The default is to delete the "
					"memory allocation goals from all manageable " NVM_DIMM_NAME "s."));
	deleteConfigGoal.addTarget(TARGET_GOAL_R);
	deleteConfigGoal.addTarget(TARGET_SOCKET.name, false, SOCKETIDS_STR, true,
			TR("Delete the memory allocation goal from the " NVM_DIMM_NAME "s on specific sockets "
			"by supplying the socket target and one or more comma-separated socket identifiers. "
			"The default is to delete the memory allocation goals from all manageable " NVM_DIMM_NAME "s on all sockets."));

	cli::framework::CommandSpec showNamespace(SHOW_NAMESPACE, TR("Show Namespace"), framework::VERB_SHOW,
			TR("Show information about one or more namespaces."));
	showNamespace.addOption(framework::OPTION_ALL);
	showNamespace.addOption(framework::OPTION_DISPLAY);
	showNamespace.addOption(framework::OPTION_UNITS).helpText(TR(NS_UNITS_OPTION_DESC.c_str()));
	showNamespace.addTarget(TARGET_NAMESPACE_R)
			.helpText(TR("Restrict output to specific namespaces by providing a comma separated list "
					"of one or more namespace identifiers. The default is to display all namespaces."));
	showNamespace.addTarget(TARGET_POOL)
			.isValueRequired(true)
			.helpText(TR("Restrict output to the namespaces on specific pools by supplying the pool "
					"target and one or more comma-separated pool identifiers. The default is to "
					"display namespaces on all pools."));
	showNamespace.addProperty(wbem::HEALTHSTATE_KEY, false,
			wbem::pmem_config::NS_HEALTH_STR_UNKNOWN + "|" + wbem::pmem_config::NS_HEALTH_STR_NORMAL + "|" +
			wbem::pmem_config::NS_HEALTH_STR_WARN + "|" + wbem::pmem_config::NS_HEALTH_STR_ERR + "|" +
			wbem::pmem_config::NS_HEALTH_STR_BROKENMIRROR, true,
			TR("Restrict output to namespaces with a specific health state by supplying the "
				"HealthState property with the desired health state. The default is to display "
				"namespaces in every state."));

	cli::framework::CommandSpec createNamespace(CREATE_NAMESPACE, TR("Create Namespace"), framework::VERB_CREATE,
			TR("Create a new namespace from a persistent memory pool of " NVM_DIMM_NAME " capacity."));
	createNamespace.addOption(framework::OPTION_FORCE);
	createNamespace.addOption(framework::OPTION_UNITS).helpText(TR(NS_UNITS_OPTION_DESC.c_str()));
	createNamespace.addTarget(TARGET_NAMESPACE_R)
			.valueText("")
			.isValueAccepted(false)
			.helpText(TR("Create a new namespace. No filtering is supported on this target."));
	createNamespace.addTarget(TARGET_POOL).isValueRequired(false)
			.helpText(TR("The pool identifier on which to create the namespace."));
	createNamespace.addProperty(CREATE_NS_PM_TYPE, false, "AppDirect|AppDirectNotInterleaved",
			true, TR("Create the namespace from persistent memory capacity in the pool that matches the type."));

	createNamespace.addProperty(CREATE_NS_PROP_FRIENDLYNAME, false, "string", true,
			TR("Optional user specified namespace name to more easily identify the namespace. Up to a maximum of 64 "
			"characters."));
	createNamespace.addProperty(CREATE_NS_PROP_OPTIMIZE, false, "CopyOnWrite|None", true,
			TR("If the namespace has CopyOnWrite optimization turned on after creation."));
	createNamespace.addProperty(CREATE_NS_PROP_ENABLED, false, "0|1", true,
			TR("Enable or disable the namespace after creation. "
					"A disabled namespace is hidden from the OS by the driver."));
	createNamespace.addProperty(CREATE_NS_PROP_ENCRYPTION, false, "No|Yes|Ignore", true,
			TR("If the namespace has Encryption turned on after creation."));
	createNamespace.addProperty(CREATE_NS_PROP_ERASECAPABLE, false, "No|Yes|Ignore", true,
			TR("If the namespace supports erase capability after creation."));
	createNamespace.addProperty(CREATE_NS_PROP_CAPACITY, false, "GiB", true,
			TR("The size of the namespace. Unless the units option is provided, "
					"capacity is expected in GiB."));
	createNamespace.addProperty(CREATE_NS_PROP_MEMORYPAGEALLOCATION, false, "None|DRAM|AppDirect", true,
			TR("Support access to the AppDirect namespace capacity using legacy memory page protocols "
					"such as DMA/RDMA by specifying where to create the underlying OS structures."));

	cli::framework::CommandSpec modifyNamespace(MODIFY_NAMESPACE, TR("Modify Namespace"), framework::VERB_SET,
			TR("Modify one or more existing namespaces."));
	modifyNamespace.addOption(framework::OPTION_FORCE);
	modifyNamespace.addOption(framework::OPTION_UNITS).helpText(TR(NS_UNITS_OPTION_DESC.c_str()));
	modifyNamespace.addTarget(TARGET_NAMESPACE_R).helpText(TR("Modify the settings on specific namespaces by "
			"providing comma separated list of one or more namespace identifiers. The default is to modify all namespaces."));
	modifyNamespace.addProperty(CREATE_NS_PROP_FRIENDLYNAME, false, "string", true,
			TR("Change the user specified namespace name up to a maximum of 64 characters."));
	modifyNamespace.addProperty(CREATE_NS_PROP_ENABLED, false, "0|1", true,
			TR("Enable or disable the namespace.  A disabled namespace is hidden from the OS by the "
			"driver."));
	modifyNamespace.addProperty(CREATE_NS_PROP_CAPACITY, false, "GiB", true,
			TR("Change the size of the namespace. Unless the units option is provided, "
					"capacity is expected in GiB."));

	cli::framework::CommandSpec deleteNamespace(DELETE_NAMESPACE, TR("Delete Namespace"), framework::VERB_DELETE,
			TR("Delete one or more existing namespaces. All data on the deleted namespace(s) becomes "
					"inaccessible."));
	deleteNamespace.addOption(framework::OPTION_FORCE);
	deleteNamespace.addTarget(TARGET_NAMESPACE_R).helpText(TR("Delete specific namespaces by providing "
			"a comma separated list of one or more namespace identifiers. The default is to "
			"delete all namespaces."));

	framework::CommandSpec showPools(SHOW_POOLS, TR("Show Persistent Memory"), framework::VERB_SHOW,
			TR("Retrieve a list of persistent memory pools of " NVM_DIMM_NAME " capacity."));
	showPools.addOption(framework::OPTION_DISPLAY);
	showPools.addOption(framework::OPTION_ALL);
	showPools.addOption(framework::OPTION_UNITS).helpText(TR("Change the units the pool capacities are displayed in."));
	showPools.addTarget(TARGET_POOL_R).helpText(TR("Restrict output to specific persistent "
			"memory pools by providing one or more comma-separated pool identifiers. "
			"The default is to display the persistent memory pools across all "
			"manageable " NVM_DIMM_NAME "s."));
	showPools.addTarget(TARGET_SOCKET).helpText(TR("Restrict output to the persistent memory "
			"pools on specific sockets by supplying the socket target and one or more "
			"comma-separated socket identifiers. The default is to display all sockets."));

	framework::CommandSpec dumpConfig(DUMP_CONFIG, TR("Dump Memory Allocation Settings"), framework::VERB_DUMP,
			TR("Store the currently configured memory allocation settings for all " NVM_DIMM_NAME "s in the "
					"system to a file in order to replicate the configuration elsewhere. Apply the stored "
					"memory allocation settings using the Load Memory Allocation Goal command."));
	dumpConfig.addOption(framework::OPTION_DESTINATION_R).helpText(TR("The file path in which to store "
			"the memory allocation settings. The resulting file will contain the memory allocation settings "
			"for all configured " NVM_DIMM_NAME "s in the system."));
	dumpConfig.addTarget(TARGET_SYSTEM_R).helpText(TR("The host system."))
			.isValueAccepted(false);
	dumpConfig.addTarget(TARGET_CONFIG_R).helpText(TR("The current " NVM_DIMM_NAME " memory allocation settings."))
			.isValueAccepted(false);

	framework::CommandSpec loadGoal(LOAD_CONFIG_GOAL, TR("Load Memory Allocation Goal"), framework::VERB_LOAD,
			TR("Load a memory allocation goal from a file onto one or more " NVM_DIMM_NAME "s."));
	loadGoal.addOption(framework::OPTION_FORCE).helpText(TR("Reconfiguring " NVM_DIMM_NAME "s is a destructive operation "
			"which requires confirmation from the user. This option suppresses the confirmation."));
	loadGoal.addOption(framework::OPTION_SOURCE_R).helpText(TR("File path of the stored configuration "
			"settings to load as a memory allocation goal."));
	loadGoal.addOption(framework::OPTION_UNITS)
			.helpText(TR("Change the units that capacities are displayed in for this command."));
	loadGoal.addTarget(TARGET_GOAL_R).isValueAccepted(false);
	loadGoal.addTarget(TARGET_DIMM)
			.helpText(TR("Load the memory allocation goal to specific " NVM_DIMM_NAME "s "
			"by supplying one or more comma-separated " NVM_DIMM_NAME " identifiers. The default is to load the "
			"memory allocation goal onto all manageable " NVM_DIMM_NAME "s."))
			.isValueRequired(true);
	loadGoal.addTarget(TARGET_SOCKET).helpText(TR("Load the memory allocation goal onto all manageable " NVM_DIMM_NAME "s on "
			"specific sockets by supplying the socket target and one or more comma-separated socket identifiers. "
			"The default is to load the memory allocation goal onto all manageable " NVM_DIMM_NAME "s on all sockets."));

	list.push_back(showNamespace);
	list.push_back(createNamespace);
	list.push_back(modifyNamespace);
	list.push_back(deleteNamespace);
	list.push_back(ShowGoalCommand::getCommandSpec(SHOW_CONFIG_GOAL));
	list.push_back(deleteConfigGoal);
	list.push_back(CreateGoalCommand::getCommandSpec(CREATE_GOAL));
	list.push_back(showPools);
	list.push_back(dumpConfig);
	list.push_back(loadGoal);
 }

// Constructor, just calls super class
cli::nvmcli::NamespaceFeature::NamespaceFeature() : cli::nvmcli::VerboseFeatureBase(),
	m_deleteNamespace(wbemDeleteNamespace),
	m_getSupportedSizeRange(wbemGetSupportedSizeRange),
	m_poolUid(""), m_blockSize(1),
	m_blockCount(0), m_capacityUnits(""),
	m_capacityExists(false), m_capacityBytes(0),
	m_friendlyName(""), m_friendlyNameExists(false),
	m_enableState(0), m_enabledStateExists(false),
	m_optimize(0), m_encryption(0), m_eraseCapable(0),
	m_channelSize(wbem::mem_config::MEMORYALLOCATIONSETTINGS_EXPONENT_UNKNOWN),
	m_controllerSize(wbem::mem_config::MEMORYALLOCATIONSETTINGS_EXPONENT_UNKNOWN),
	m_byOne(false), m_forceOption(false),
	m_memoryPageAllocation(0), m_optimizeExists(false),
	m_pPmNamespaceProvider(new wbem::pmem_config::PersistentMemoryNamespaceFactory()),
	m_pPmServiceProvider(new wbem::pmem_config::PersistentMemoryServiceFactory()),
	m_pPmPoolProvider(new wbem::pmem_config::PersistentMemoryPoolFactory()),
	m_pCapProvider(new wbem::pmem_config::PersistentMemoryCapabilitiesFactory()),
	m_pNsViewFactoryProvider(new wbem::pmem_config::NamespaceViewFactory()),
	m_pPoolViewProvider(new wbem::mem_config::PoolViewFactory()),
	m_pWbemToCli(new cli::nvmcli::WbemToCli())
{
}

cli::nvmcli::NamespaceFeature::~NamespaceFeature()
{
	delete m_pPmServiceProvider;
	delete m_pPmPoolProvider;
	delete m_pCapProvider;
	delete m_pNsViewFactoryProvider;
	delete m_pWbemToCli;
	delete m_pPmNamespaceProvider;
	delete m_pPoolViewProvider;
}

/*
 * Get all the BaseServer Instances from the wbem base server factory.
 */
cli::framework::ResultBase * cli::nvmcli::NamespaceFeature::run(
		const int &commandSpecId, const framework::ParsedCommand& parsedCommand)
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);

	framework::ResultBase *pResult = NULL;

	enableVerbose(parsedCommand);

	switch (commandSpecId)
	{
		case SHOW_CONFIG_GOAL:
			pResult = showConfigGoal(parsedCommand);
			break;
		case DELETE_CONFIG_GOAL:
			pResult = deleteConfigGoal(parsedCommand);
			break;
		case CREATE_GOAL:
			pResult = createGoal(parsedCommand);
			break;
		case SHOW_POOLS:
			pResult = showPools(parsedCommand);
			break;
		case DUMP_CONFIG:
			pResult = dumpConfig(parsedCommand);
			break;
		case LOAD_CONFIG_GOAL:
			pResult = loadGoal(parsedCommand);
			break;
		case SHOW_NAMESPACE:
			pResult = showNamespaces(parsedCommand);
			break;
		case DELETE_NAMESPACE:
			pResult = deleteNamespaces(parsedCommand);
			break;
		case CREATE_NAMESPACE:
			pResult = createNamespace(parsedCommand);
			break;
		case MODIFY_NAMESPACE:
			pResult = modifyNamespace(parsedCommand);
			break;
		default:
			pResult = new framework::NotImplementedErrorResult(commandSpecId, Name);
			break;
	}

	disableVerbose(parsedCommand);

	return pResult;
}

cli::framework::ResultBase* cli::nvmcli::NamespaceFeature::showConfigGoal(
		const framework::ParsedCommand& parsedCommand)
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);

	ShowGoalCommand command;
	return command.execute(parsedCommand);
}

cli::framework::ResultBase *cli::nvmcli::NamespaceFeature::createGoal(
	const cli::framework::ParsedCommand &parsedCommand)
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);
	cli::framework::ResultBase *pResult;
	try
	{
		core::memory_allocator::MemoryAllocator *pAllocator =
			core::memory_allocator::MemoryAllocator::getNewMemoryAllocator();
		core::memory_allocator::MemoryAllocationRequestBuilder builder;
		CreateGoalCommand::ShowGoalAdapter showGoalAdapter;
		framework::YesNoPrompt yesNoPrompt;
		cli::nvmcli::CreateGoalCommand::UserPrompt prompt(yesNoPrompt, showGoalAdapter);

		cli::nvmcli::CreateGoalCommand cmd(*pAllocator, builder, prompt, showGoalAdapter);
		pResult = cmd.execute(parsedCommand);
		delete (pAllocator);
	}
	catch (std::exception &e)
	{
		pResult = CoreExceptionToResult(e);
	}

	return pResult;
}

cli::framework::ResultBase *cli::nvmcli::NamespaceFeature::showPools(cli::framework::ParsedCommand const &parsedCommand)
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);

	framework::ResultBase *pResult = NULL;

	wbem::framework::attribute_names_t attributes;
	wbem::framework::instances_t *pInstances = NULL;

	// get the desired units of capacity
	std::string capacityUnits;
	pResult = GetRequestedCapacityUnits(parsedCommand, capacityUnits);
	if (!pResult)
	{
		try
		{
			// define default display attributes
			wbem::framework::attribute_names_t defaultAttributes;
			defaultAttributes.push_back(wbem::POOLID_KEY);
			defaultAttributes.push_back(wbem::PERSISTENTMEMORYTYPE_KEY);
			defaultAttributes.push_back(wbem::CAPACITY_KEY);
			defaultAttributes.push_back(wbem::FREECAPACITY_KEY);

			// define all attributes
			wbem::framework::attribute_names_t allAttributes(defaultAttributes);
			allAttributes.push_back(wbem::ENCRYPTIONCAPABLE_KEY);
			allAttributes.push_back(wbem::ENCRYPTIONENABLED_KEY);
			allAttributes.push_back(wbem::ERASECAPABLE_KEY);
			allAttributes.push_back(wbem::SOCKETID_KEY);
			allAttributes.push_back(wbem::APPDIRECTNAMESPACE_MAX_SIZE_KEY);
			allAttributes.push_back(wbem::APPDIRECTNAMESPACE_MIN_SIZE_KEY);
			allAttributes.push_back(wbem::APPDIRECTNAMESPACE_COUNT_KEY);
			allAttributes.push_back(wbem::HEALTHSTATE_KEY);
			allAttributes.push_back(wbem::ACTIONREQUIRED_KEY);
			allAttributes.push_back(wbem::ACTIONREQUIREDEVENTS_KEY);

			// get the desired attributes
			wbem::framework::attribute_names_t attributes =
					GetAttributeNames(parsedCommand.options, defaultAttributes, allAttributes);

			// make sure we have the Pool  id in our display
			// this would cover the case the user asks for specific display attributes, but they
			// don't include the physical ID
			if (!wbem::framework_interface::NvmInstanceFactory::containsAttribute(wbem::POOLID_KEY,
					attributes))
			{
				attributes.insert(attributes.begin(), wbem::POOLID_KEY);
			}

			// create the display filters
			wbem::framework::attribute_names_t requestedAttributes = attributes;
			cli::nvmcli::filters_t filters;
			generateSocketFilter(parsedCommand, requestedAttributes, filters);
			if (NULL == (pResult = generatePoolFilter(parsedCommand, requestedAttributes, filters)))
			{
				pInstances = m_pPoolViewProvider->getInstances(requestedAttributes);
				if (pInstances == NULL)
				{
					COMMON_LOG_ERROR("PoolViewFactory getInstances returned a NULL instances pointer");
					pResult = new framework::ErrorResult(framework::ErrorResult::ERRORCODE_UNKNOWN,
							TRS(nvmcli::UNKNOWN_ERROR_STR));
				}
				else
				{
					for (size_t i = 0; i < pInstances->size(); i++)
					{
						cli::nvmcli::convertCapacityAttribute((*pInstances)[i], wbem::CAPACITY_KEY, capacityUnits);
						cli::nvmcli::convertCapacityAttribute((*pInstances)[i], wbem::FREECAPACITY_KEY, capacityUnits);
						cli::nvmcli::convertCapacityAttribute((*pInstances)[i], wbem::APPDIRECTNAMESPACE_MAX_SIZE_KEY, capacityUnits);
						cli::nvmcli::convertCapacityAttribute((*pInstances)[i], wbem::APPDIRECTNAMESPACE_MIN_SIZE_KEY, capacityUnits);
					}
					pResult = NvmInstanceToObjectListResult(*pInstances, "Pool",
							wbem::POOLID_KEY, attributes, filters);
					// Set layout to table unless the -all or -display option is present
					if (!framework::parsedCommandContains(parsedCommand, framework::OPTION_DISPLAY) &&
							!framework::parsedCommandContains(parsedCommand, framework::OPTION_ALL))
					{
						pResult->setOutputType(framework::ResultBase::OUTPUT_TEXTTABLE);
					}
				}
			}
		}
		catch (wbem::framework::Exception &e)
		{
			if (pResult)
			{
				delete pResult;
				pResult = NULL;
			}
			pResult = NvmExceptionToResult(e);
		}
	}
	if (pInstances)
	{
		delete pInstances;
	}

	return pResult;
}

/*
 * create filters for pool ID
 */
cli::framework::ErrorResult * cli::nvmcli::NamespaceFeature::generatePoolFilter(
		const cli::framework::ParsedCommand &parsedCommand,
		wbem::framework::attribute_names_t &attributes,
		cli::nvmcli::filters_t &filters)
{
	LogEnterExit logging(__FUNCTION__, __FILE__, __LINE__);

	framework::ErrorResult *pError = NULL;
	std::vector<std::string> poolTargets =
			cli::framework::Parser::getTargetValues(parsedCommand,
					cli::nvmcli::TARGET_POOL.name);
	if (!poolTargets.empty())
	{
		struct instanceFilter poolFilter;
		poolFilter.attributeName = wbem::POOLID_KEY;

		std::vector<struct pool> allPools = m_pPoolViewProvider->getPoolList(true);

		for (std::vector<std::string>::const_iterator poolTargetIter = poolTargets.begin();
			 poolTargetIter != poolTargets.end() && !pError; poolTargetIter++)
		{
			std::string target = (*poolTargetIter);
			bool found = false;
			for (size_t i = 0; i < allPools.size() && !found; i++)
			{
				if (framework::stringsIEqual(allPools[i].pool_uid, target))
				{
					found = true;
				}
			}

			if(found)
			{
				poolFilter.attributeValues.push_back(target);
			}
			else
			{
				pError = new framework::ErrorResult(framework::ErrorResult::ERRORCODE_UNKNOWN,
					getInvalidPoolIdErrorString(target));
			}
		}

		if (!poolFilter.attributeValues.empty())
		{
			filters.push_back(poolFilter);
			// make sure we have the Pool ID filter attribute
			if (!wbem::framework_interface::NvmInstanceFactory::containsAttribute(wbem::POOLID_KEY, attributes))
			{
				attributes.insert(attributes.begin(), wbem::POOLID_KEY);
			}
		}
	}
	return pError;
}

void cli::nvmcli::NamespaceFeature::setPersistentMemoryNamespaceProvider(
		wbem::pmem_config::PersistentMemoryNamespaceFactory *pProvider)
{
	SET_PROVIDER(m_pPmNamespaceProvider, pProvider);
}

void cli::nvmcli::NamespaceFeature::setPersistentMemoryServiceProvider(
		wbem::pmem_config::PersistentMemoryServiceFactory *pProvider)
{
	SET_PROVIDER(m_pPmServiceProvider, pProvider);
}

void cli::nvmcli::NamespaceFeature::setPmPoolProvider(
		wbem::pmem_config::PersistentMemoryPoolFactory *pProvider)
{
	SET_PROVIDER(m_pPmPoolProvider, pProvider);
}

void cli::nvmcli::NamespaceFeature::setCapabilitiesProvider(
		wbem::pmem_config::PersistentMemoryCapabilitiesFactory *pProvider)
{
	SET_PROVIDER(m_pCapProvider, pProvider);
}

void cli::nvmcli::NamespaceFeature::setNsViewProvider(
		wbem::pmem_config::NamespaceViewFactory *pProvider)
{
	SET_PROVIDER(m_pNsViewFactoryProvider, pProvider);
}

void cli::nvmcli::NamespaceFeature::setPoolViewProvider(
		wbem::mem_config::PoolViewFactory *pProvider)
{
	SET_PROVIDER(m_pPoolViewProvider, pProvider);
}

void cli::nvmcli::NamespaceFeature::setWbemToCli(
		cli::nvmcli::WbemToCli *pInstance)
{
	SET_PROVIDER(m_pWbemToCli, pInstance);
}
