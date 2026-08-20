from setuptools import find_packages, setup

package_name = 'topic_demo'

setup(
    name=package_name,
    version='0.0.0',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
    ],
    install_requires=['setuptools'],
    zip_safe=True,
    maintainer='mu',
    maintainer_email='mu@todo.todo',
    description='TODO: Package description',
    license='TODO: License declaration',
    extras_require={
        'test': [
            'pytest',
        ],
    },
    entry_points={
        'console_scripts': [
            # 常驻节点：topic + service + callback 客户端
            'py_topic = topic_demo.my_topic:main',
            # 单独客户端：Future + spin_until_future_complete
            'client_future = topic_demo.client_future:main',
        ],
    },
)
