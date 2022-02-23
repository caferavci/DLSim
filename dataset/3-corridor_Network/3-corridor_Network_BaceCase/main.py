# @author       Xuesong Zhou (xzhou74@asu.edu)
# @time         2022/1/4 13:03
# @desc         [script description]


import osm2gmns as og
import os


# NOTE: WHEN USING THIS SCRIPT, ONLY EXECUTE COMMANDS FOR THE STAGE THAT YOU ARE CURRENTLY WORKING ON.
#       COMMENT COMMANDS FOR OTHER SATGES IN THE MAIN FUNCTION.


map_folder = r'network'

def getInitialNet():
    # choose link_types from 'motorway', 'trunk','primary','secondary', 'tertiary', 'residential'. default: 'all'
    net = og.getNetFromFile(filename=os.path.join(map_folder, 'map.osm'),
                            link_types=('motorway', 'trunk', 'primary', 'secondary', 'tertiary'),
                            default_lanes=True, default_speed=True, default_capacity=True)

    og.consolidateComplexIntersections(net, auto_identify=True)
    og.generateNodeActivityInfo(net)
    og.buildMultiResolutionNets(net)

    og.outputNetToCSV(net, output_folder=map_folder)


def regenerateNet_based_on_macroNet():
    net = og.loadNetFromCSV(folder=map_folder,
                            node_file='node.csv', link_file='link.csv')

    # The consolidation function is needed if we changed 'main_node_id' of some nodes in node.csv in Stage 2.
    #og.consolidateComplexIntersections(net, auto_identify=False)

    og.buildMultiResolutionNets(net)

    og.outputNetToCSV(net, output_folder=map_folder)


if __name__ == '__main__':
    # Stage 1: get an initial network
    #getInitialNet()

    # Stage 2: modify the initial network in CSV files

    # Stage 3: read the modified network from CSV and regenerate multiresolution networks
     regenerateNet_based_on_macroNet()


